# frozen_string_literal: true

module Zinc
  class Codegen
    # --- Collection callback helpers ---

    def emit_elem_retain_cb(elem)
      if !elem then emit('NULL')
      elsif elem.kind == TK_STRING then emit('(ZnElemFn)__zn_str_retain_v')
      elsif elem.kind == TK_ARRAY then emit('(ZnElemFn)__zn_arr_retain_v')
      elsif elem.kind == TK_HASH then emit('(ZnElemFn)__zn_hash_retain_v')
      elsif elem.kind == TK_CLASS && elem.name then emit("(ZnElemFn)__zn_ret_#{elem.name}")
      else emit('NULL')
      end
    end

    def emit_elem_release_cb(elem)
      if !elem then emit('NULL')
      elsif elem.kind == TK_STRING then emit('(ZnElemFn)__zn_str_release_v')
      elsif elem.kind == TK_ARRAY then emit('(ZnElemFn)__zn_arr_release_v')
      elsif elem.kind == TK_HASH then emit('(ZnElemFn)__zn_hash_release_v')
      elsif elem.kind == TK_CLASS && elem.name then emit("(ZnElemFn)__zn_rel_#{elem.name}")
      elsif elem.kind == TK_STRUCT && elem.name then emit("(ZnElemFn)__zn_val_rel_#{elem.name}")
      else emit('NULL')
      end
    end

    def emit_hashcode_cb(elem)
      if elem&.kind == TK_STRUCT && elem.name
        emit("__zn_hash_#{elem.name}")
      else
        emit('__zn_default_hashcode')
      end
    end

    def emit_equals_cb(elem)
      if elem&.kind == TK_STRUCT && elem.name
        emit("__zn_eq_#{elem.name}")
      else
        emit('__zn_default_equals')
      end
    end

    def emit_arr_callbacks(elem)
      emit(', '); emit_elem_retain_cb(elem)
      emit(', '); emit_elem_release_cb(elem)
      emit(', '); emit_hashcode_cb(elem)
      emit(', '); emit_equals_cb(elem)
    end

    def emit_hash_callbacks(key, val)
      emit(', '); emit_elem_retain_cb(key)
      emit(', '); emit_elem_release_cb(key)
      emit(', '); emit_hashcode_cb(key)
      emit(', '); emit_equals_cb(key)
      emit(', '); emit_elem_retain_cb(val)
      emit(', '); emit_elem_release_cb(val)
    end

    # Emit retain for a named variable (skip if fresh alloc)
    def emit_retain(name, value, type)
      return if !type || (value&.is_fresh_alloc)
      if ref_type?(type.kind)
        emit_indent
        emit_retain_call(name, type)
        emit(";\n")
      end
    end

    # Emit inline retain for expression temp
    def emit_inline_retain(temp_id, prefix, value, type)
      return if !type || !value || !ref_type?(type.kind) || value.is_fresh_alloc
      emit_retain_call("#{prefix}#{temp_id}", type)
      emit('; ')
    end

    # Check if struct has any ref-counted fields
    def struct_has_rc_fields(sd)
      fd = sd.fields
      while fd
        if fd.type
          return true if [TK_STRING, TK_ARRAY, TK_HASH, TK_CLASS].include?(fd.type.kind)
          if fd.type.kind == TK_STRUCT && fd.type.name
            inner = @sem.lookup_struct(fd.type.name)
            return true if inner && struct_has_rc_fields(inner)
          end
        end
        fd = fd.next
      end
      false
    end

    # Emit temp variable declaration for ref type
    def emit_ref_temp_decl(name, type)
      if type.kind == TK_CLASS && type.name
        emit("#{type.name} *#{name} = ")
      else
        emit("#{type_to_c(type.kind)} #{name} = ")
      end
    end

    # Track a ref-type variable in ARC scope
    def scope_track_ref(name, type)
      return unless @scope && type
      case type.kind
      when TK_STRING then scope_add_ref(name, 'zn_str')
      when TK_CLASS then scope_add_ref(name, type.name) if type.name
      when TK_ARRAY then scope_add_ref(name, 'zn_arr')
      when TK_HASH then scope_add_ref(name, 'zn_hash')
      when TK_STRUCT
        if type.name
          sd = @sem.lookup_struct(type.name)
          scope_add_value_type(name, type.name) if sd && struct_has_rc_fields(sd)
        end
      end
    end

    def unbox_func_for(t)
      case t
      when TK_INT    then '__zn_val_as_int'
      when TK_FLOAT  then '__zn_val_as_float'
      when TK_BOOL   then '__zn_val_as_bool'
      when TK_CHAR   then '__zn_val_as_char'
      when TK_STRING then '__zn_val_as_string'
      else '__zn_val_as_int'
      end
    end

    # Box an expression into ZnValue
    def gen_box_expr(expr)
      t = expr.resolved_type.kind
      case t
      when TK_INT    then emit('__zn_val_int('); gen_expr(expr); emit(')')
      when TK_FLOAT  then emit('__zn_val_float('); gen_expr(expr); emit(')')
      when TK_BOOL   then emit('__zn_val_bool('); gen_expr(expr); emit(')')
      when TK_CHAR   then emit('__zn_val_char('); gen_expr(expr); emit(')')
      when TK_STRING then emit('__zn_val_string('); gen_expr(expr); emit(')')
      when TK_ARRAY  then emit('__zn_val_array((ZnArray*)('); gen_expr(expr); emit('))')
      when TK_HASH   then emit('__zn_val_hash((ZnHash*)('); gen_expr(expr); emit('))')
      when TK_CLASS  then emit('__zn_val_ref('); gen_expr(expr); emit(')')
      when TK_STRUCT
        if expr.resolved_type.name
          name = expr.resolved_type.name
          emit("__zn_val_val(({ #{name} *__cp = malloc(sizeof(#{name})); *__cp = (")
          gen_expr(expr)
          emit('); __cp; }))')
        else
          emit('__zn_val_int((int64_t)('); gen_expr(expr); emit('))')
        end
      else
        emit('__zn_val_int((int64_t)('); gen_expr(expr); emit('))')
      end
    end

    # Emit for loop header
    def gen_for_header(node)
      emit('for (')
      init = node.init
      if init
        if init.is_a?(AST::Decl)
          t = init.value.resolved_type.kind
          cq = init.is_const ? 'const ' : ''
          emit("#{cq}#{type_to_c(t)} #{init.name} = ")
          gen_expr(init.value)
        else
          gen_expr(init)
        end
      end
      emit('; ')
      gen_expr(node.cond)
      emit('; ')
      gen_expr(node.update) if node.update
      emit(') ')
    end

    # Generate string comparison using strcmp
    def gen_string_comparison(left, op_str, right)
      emit('(strcmp((')
      gen_expr(left)
      emit(')->_data, (')
      gen_expr(right)
      emit(")->_data) #{op_str} 0)")
    end

    # Emit coercion wrapper for non-string operand in concat
    def gen_coerce_to_string(expr)
      t = expr.resolved_type.kind
      if t == TK_STRING
        gen_expr(expr)
        return
      end
      case t
      when TK_INT   then emit('__zn_str_from_int(')
      when TK_FLOAT then emit('__zn_str_from_float(')
      when TK_BOOL  then emit('__zn_str_from_bool(')
      when TK_CHAR  then emit('__zn_str_from_char(')
      else gen_expr(expr); return
      end
      gen_expr(expr)
      emit(')')
    end

    # Count string concat operands
    def count_string_concat_parts(expr)
      return 0 unless expr
      if expr.is_a?(AST::BinOp) && expr.resolved_type&.kind == TK_STRING && expr.op == Op::ADD
        return count_string_concat_parts(expr.left) + count_string_concat_parts(expr.right)
      end
      1
    end

    # Flatten string concat tree
    def flatten_string_concat(expr, leaves)
      if expr.is_a?(AST::BinOp) && expr.resolved_type&.kind == TK_STRING && expr.op == Op::ADD
        flatten_string_concat(expr.left, leaves)
        flatten_string_concat(expr.right, leaves)
      else
        leaves << expr
      end
    end

    # Generate string concatenation
    def gen_string_concat(expr)
      leaves = []
      flatten_string_concat(expr, leaves)

      emit('({ ')

      # Pre-evaluate non-string leaves into coercion temps
      coerce_temp = Array.new(leaves.size, -1)
      leaves.each_with_index do |leaf, i|
        if leaf.resolved_type.kind != TK_STRING
          c = @temp_counter; @temp_counter += 1
          coerce_temp[i] = c
          emit("ZnString *__c#{c} = ")
          gen_coerce_to_string(leaf)
          emit('; ')
        end
      end

      base_temp = @temp_counter
      @temp_counter += leaves.size - 1

      (leaves.size - 1).times do |i|
        t = base_temp + i
        emit("ZnString *__t#{t} = __zn_str_concat(")
        if i == 0
          if coerce_temp[0] >= 0
            emit("__c#{coerce_temp[0]}")
          else
            gen_expr(leaves[0])
          end
        else
          emit("__t#{t - 1}")
        end
        emit(', ')
        if coerce_temp[i + 1] >= 0
          emit("__c#{coerce_temp[i + 1]}")
        else
          gen_expr(leaves[i + 1])
        end
        emit('); ')

        # Release coerced non-string temps
        if i == 0 && coerce_temp[0] >= 0
          emit("__zn_str_release(__c#{coerce_temp[0]}); ")
        end
        if coerce_temp[i + 1] >= 0
          emit("__zn_str_release(__c#{coerce_temp[i + 1]}); ")
        end

        # Release previous intermediate
        if i > 0
          emit("__zn_str_release(__t#{t - 1}); ")
        end
      end

      emit("__t#{base_temp + leaves.size - 2}; })")
    end

    def gen_block_with_scope(block, is_loop)
      return unless block.is_a?(AST::Block)
      emit("{\n")
      @indent_level += 1
      push_scope(is_loop)
      gen_stmts(block.stmts)
      emit_scope_releases
      pop_scope
      @indent_level -= 1
      emit_indent
      emit('}')
    end

    def gen_block(block)
      gen_block_with_scope(block, false)
    end

    def gen_expr(expr)
      return unless expr

      case expr
      when AST::IntLit
        emit(expr.value.to_s)

      when AST::FloatLit
        emit(sprintf('%g', expr.value))

      when AST::StringLit
        emit("(ZnString*)&__zn_str_#{expr.string_id}")

      when AST::BoolLit
        emit(expr.value ? 'true' : 'false')

      when AST::CharLit
        emit("'")
        ch = expr.value
        case ch
        when "\n" then emit('\n')
        when "\t" then emit('\t')
        when "\r" then emit('\r')
        when "\\" then emit('\\\\')
        when "'"  then emit("\\'")
        when "\0" then emit('\0')
        else emit(ch)
        end
        emit("'")

      when AST::Ident
        # Check if narrowed
        if @narrowed.include?(expr.name)
          emit("#{expr.name}._val")
        else
          emit(expr.name)
        end

      when AST::BinOp
        gen_binop_expr(expr)

      when AST::UnaryOp
        emit('(')
        emit(Op.to_s(expr.op))
        gen_expr(expr.operand)
        emit(')')

      when AST::Assign
        gen_expr(expr.target)
        emit(' = ')
        gen_expr(expr.value)

      when AST::CompoundAssign
        gen_expr(expr.target)
        emit(" #{Op.to_s(expr.op)} ")
        gen_expr(expr.value)

      when AST::IncDec
        if expr.is_prefix
          emit(Op.to_s(expr.op))
          gen_expr(expr.target)
        else
          gen_expr(expr.target)
          emit(Op.to_s(expr.op))
        end

      when AST::Call
        gen_call_expr(expr)

      when AST::FieldAccess
        gen_field_access_expr(expr)

      when AST::Tuple
        gen_tuple_expr(expr)

      when AST::ObjectLiteral
        gen_object_literal_expr(expr)

      when AST::Index
        gen_index_expr(expr)

      when AST::ArrayLiteral
        gen_array_literal_expr(expr)

      when AST::HashLiteral
        gen_hash_literal_expr(expr)

      when AST::TypedEmptyArray
        gen_typed_empty_array_expr(expr)

      when AST::TypedEmptyHash
        gen_typed_empty_hash_expr(expr)

      when AST::OptionalCheck
        gen_optional_check_expr(expr)

      when AST::If
        gen_if_expr(expr)

      when AST::While
        gen_while_expr(expr)

      when AST::For
        gen_for_expr(expr)
      end
    end

    def gen_binop_expr(expr)
      op = expr.op
      is_comparison = [Op::EQ, Op::NE, Op::LT, Op::GT, Op::LE, Op::GE].include?(op)

      # String concatenation
      if op == Op::ADD && expr.resolved_type&.kind == TK_STRING
        gen_string_concat(expr)
        return
      end

      # String comparison
      if is_comparison && (expr_is_string(expr.left) || expr_is_string(expr.right))
        gen_string_comparison(expr.left, Op.to_s(op), expr.right)
      else
        emit('(')
        gen_expr(expr.left)
        emit(" #{Op.to_s(op)} ")
        gen_expr(expr.right)
        emit(')')
      end
    end

    def gen_call_expr(expr)
      # Built-in print
      if expr.name == 'print'
        emit('({ fputs((')
        gen_expr(expr.args[0]) unless expr.args.empty?
        emit(')->_data, stdout); })')
        return
      end

      if expr.is_struct_init
        gen_struct_init_expr(expr)
      else
        gen_regular_call_expr(expr)
      end
    end

    def gen_struct_init_expr(expr)
      name = expr.name
      sd = @sem.lookup_struct(name)
      return unless sd

      if sd.is_class
        # Class init: heap allocate
        t = @temp_counter; @temp_counter += 1
        emit("({ #{name} *__ci_#{t} = __#{name}_alloc(); ")

        fd = sd.fields
        while fd
          val = nil
          expr.args.each do |a|
            if a.is_a?(AST::NamedArg) && a.name == fd.name
              val = a.value
              break
            end
          end

          emit("__ci_#{t}->#{fd.name} = ")
          if val
            gen_expr(val)
          elsif fd.default_value
            gen_expr(fd.default_value)
          else
            emit('0')
          end
          emit('; ')

          # Retain reference-type fields (skip weak)
          if !fd.is_weak && ref_type?(fd.type.kind)
            if !val || !val.is_fresh_alloc
              emit_retain_call("__ci_#{t}->#{fd.name}", fd.type)
              emit('; ')
            end
          end

          fd = fd.next
        end

        emit("__ci_#{t}; })")
      else
        # Struct init: value type
        needs_arc = false
        fd = sd.fields
        while fd
          needs_arc = true if fd.type && ref_type?(fd.type.kind)
          fd = fd.next
        end

        if needs_arc
          t = @temp_counter; @temp_counter += 1
          emit("({ #{name} __vt#{t} = (#{name}){")
          first = true
          fd = sd.fields
          while fd
            emit(', ') unless first
            emit(".#{fd.name} = ")
            val = find_named_arg(expr.args, fd.name)
            if val
              gen_expr(val)
            elsif fd.default_value
              gen_expr(fd.default_value)
            else
              emit('0')
            end
            first = false
            fd = fd.next
          end
          emit('}; ')
          # Retains
          fd = sd.fields
          while fd
            if fd.type && ref_type?(fd.type.kind)
              val = find_named_arg(expr.args, fd.name)
              unless val&.is_fresh_alloc
                emit_retain_call("__vt#{t}.#{fd.name}", fd.type)
                emit('; ')
              end
            end
            fd = fd.next
          end
          emit("__vt#{t}; })")
        else
          emit("(#{name}){")
          first = true
          fd = sd.fields
          while fd
            emit(', ') unless first
            emit(".#{fd.name} = ")
            val = find_named_arg(expr.args, fd.name)
            if val
              gen_expr(val)
            elsif fd.default_value
              gen_expr(fd.default_value)
            else
              emit('0')
            end
            first = false
            fd = fd.next
          end
          emit('}')
        end
      end
    end

    def gen_regular_call_expr(expr)
      emit(expr.name)
      emit('(')
      func_sym = @sem.lookup(expr.name)
      expr.args.each_with_index do |a, i|
        emit(', ') if i > 0
        # Wrap non-optional value in optional struct if param expects it
        wrap_opt = false
        opt_wrap = nil
        if func_sym && i < func_sym.param_count &&
           func_sym.param_types&.[](i)&.is_optional
          at = a.resolved_type
          if !at || !at.is_optional
            opt_wrap = opt_type_for(func_sym.param_types[i].kind)
            wrap_opt = true if opt_wrap
          end
        end
        if wrap_opt
          emit("(#{opt_wrap}){._has = true, ._val = ")
          gen_expr(a)
          emit('}')
        else
          gen_expr(a)
        end
      end
      emit(')')
    end

    def gen_field_access_expr(expr)
      obj = expr.object
      field = expr.field
      obj_kind = obj.resolved_type&.kind

      # String/Array/Hash .length
      if (obj_kind == TK_STRING || obj_kind == TK_ARRAY || obj_kind == TK_HASH) && field == 'length'
        emit('(int64_t)((')
        gen_expr(obj)
        emit(')->_len)')
        return
      end

      # Struct/class field: -> for classes, . for value types
      gen_expr(obj)
      if obj_kind == TK_CLASS
        emit("->#{field}")
      else
        emit(".#{field}")
      end
    end

    def gen_tuple_expr(expr)
      name = expr.resolved_type&.name
      sd = @sem.lookup_struct(name)

      needs_arc = false
      if sd
        fd = sd.fields
        while fd
          needs_arc = true if fd.type && ref_type?(fd.type.kind)
          fd = fd.next
        end
      end

      if needs_arc
        t = @temp_counter; @temp_counter += 1
        emit("({ #{name} __vt#{t} = (#{name}){")
        if sd
          fd = sd.fields
          first = true
          expr.elements.each do |e|
            break unless fd
            emit(', ') unless first
            emit(".#{fd.name} = ")
            if e.is_a?(AST::NamedArg)
              gen_expr(e.value)
            else
              gen_expr(e)
            end
            first = false
            fd = fd.next
          end
        end
        emit('}; ')
        # Retains
        if sd
          fd = sd.fields
          expr.elements.each do |e|
            break unless fd
            val = e.is_a?(AST::NamedArg) ? e.value : e
            if ref_type?(fd.type.kind) && !val&.is_fresh_alloc
              emit_retain_call("__vt#{t}.#{fd.name}", fd.type)
              emit('; ')
            end
            fd = fd.next
          end
        end
        emit("__vt#{t}; })")
      else
        emit("(#{name}){")
        if sd
          fd = sd.fields
          first = true
          expr.elements.each do |e|
            break unless fd
            emit(', ') unless first
            emit(".#{fd.name} = ")
            if e.is_a?(AST::NamedArg)
              gen_expr(e.value)
            else
              gen_expr(e)
            end
            first = false
            fd = fd.next
          end
        end
        emit('}')
      end
    end

    def gen_object_literal_expr(expr)
      type_name = expr.resolved_type&.name
      t = @temp_counter; @temp_counter += 1
      emit("({ #{type_name} *__t#{t} = __#{type_name}_alloc(); ")
      expr.fields.each do |na|
        emit("__t#{t}->#{na.name} = ")
        gen_expr(na.value)
        emit('; ')

        ftype = na.value.resolved_type
        if ftype && ref_type?(ftype.kind) && !na.value.is_fresh_alloc
          emit_retain_call("__t#{t}->#{na.name}", ftype)
          emit('; ')
        end
      end
      emit("__t#{t}; })")
    end

    def gen_index_expr(expr)
      obj = expr.object
      obj_kind = obj.resolved_type&.kind

      if obj_kind == TK_ARRAY
        gen_array_index_expr(expr)
      elsif obj_kind == TK_HASH
        gen_hash_index_expr(expr)
      else
        # String indexing
        emit('(')
        gen_expr(obj)
        emit(')->_data[')
        gen_expr(expr.index)
        emit(']')
      end
    end

    def gen_array_index_expr(expr)
      arr_elem = expr.resolved_type
      if arr_elem&.kind == TK_ARRAY
        emit('(ZnArray*)__zn_arr_get('); gen_expr(expr.object); emit(', '); gen_expr(expr.index); emit(').as.ptr')
      elsif arr_elem&.kind == TK_HASH
        emit('(ZnHash*)__zn_arr_get('); gen_expr(expr.object); emit(', '); gen_expr(expr.index); emit(').as.ptr')
      elsif arr_elem&.kind == TK_CLASS && arr_elem.name
        emit("(#{arr_elem.name}*)__zn_arr_get("); gen_expr(expr.object); emit(', '); gen_expr(expr.index); emit(').as.ptr')
      elsif arr_elem&.kind == TK_STRUCT && arr_elem.name
        emit("*(#{arr_elem.name}*)__zn_arr_get("); gen_expr(expr.object); emit(', '); gen_expr(expr.index); emit(').as.ptr')
      else
        emit("#{unbox_func_for(arr_elem&.kind || TK_UNKNOWN)}(")
        emit('__zn_arr_get('); gen_expr(expr.object); emit(', '); gen_expr(expr.index); emit('))')
      end
    end

    def gen_hash_index_expr(expr)
      hash_val = expr.resolved_type
      if hash_val&.kind == TK_ARRAY
        emit('(ZnArray*)__zn_hash_get('); gen_expr(expr.object); emit(', '); gen_box_expr(expr.index); emit(').as.ptr')
      elsif hash_val&.kind == TK_HASH
        emit('(ZnHash*)__zn_hash_get('); gen_expr(expr.object); emit(', '); gen_box_expr(expr.index); emit(').as.ptr')
      elsif hash_val&.kind == TK_CLASS && hash_val.name
        emit("(#{hash_val.name}*)__zn_hash_get("); gen_expr(expr.object); emit(', '); gen_box_expr(expr.index); emit(').as.ptr')
      elsif hash_val&.kind == TK_STRUCT && hash_val.name
        emit("*(#{hash_val.name}*)__zn_hash_get("); gen_expr(expr.object); emit(', '); gen_box_expr(expr.index); emit(').as.ptr')
      else
        emit("#{unbox_func_for(hash_val&.kind || TK_UNKNOWN)}(")
        emit('__zn_hash_get('); gen_expr(expr.object); emit(', '); gen_box_expr(expr.index); emit('))')
      end
    end

    def gen_array_literal_expr(expr)
      n = expr.elems.size
      t = @temp_counter; @temp_counter += 1
      emit("({ ZnArray *__t#{t} = __zn_arr_alloc(#{n > 0 ? n : 4}")
      emit_arr_callbacks(expr.resolved_type&.elem)
      emit('); ')
      expr.elems.each do |elem|
        if elem.resolved_type && ref_type?(elem.resolved_type.kind) && elem.is_fresh_alloc
          pt = @temp_counter; @temp_counter += 1
          pname = "__pe#{pt}"
          emit_ref_temp_decl(pname, elem.resolved_type)
          gen_expr(elem)
          emit("; __zn_arr_push(__t#{t}, ")
          emit_box_call(pname, elem.resolved_type)
          emit('); ')
          emit_release_call(pname, elem.resolved_type)
          emit('; ')
        else
          emit("__zn_arr_push(__t#{t}, ")
          gen_box_expr(elem)
          emit('); ')
        end
      end
      emit("__t#{t}; })")
    end

    def gen_hash_literal_expr(expr)
      n = expr.pairs.size
      t = @temp_counter; @temp_counter += 1
      emit("({ ZnHash *__t#{t} = __zn_hash_alloc(#{n > 0 ? n * 2 : 8}")
      emit_hash_callbacks(expr.resolved_type&.key, expr.resolved_type&.elem)
      emit('); ')
      expr.pairs.each do |pair|
        hk = pair.key
        hv = pair.value
        fresh_key = hk.resolved_type && ref_type?(hk.resolved_type.kind) && hk.is_fresh_alloc
        fresh_val = hv.resolved_type && ref_type?(hv.resolved_type.kind) && hv.is_fresh_alloc
        kname = nil; vname = nil
        if fresh_key
          kt = @temp_counter; @temp_counter += 1
          kname = "__pk#{kt}"
          emit_ref_temp_decl(kname, hk.resolved_type)
          gen_expr(hk)
          emit('; ')
        end
        if fresh_val
          vt = @temp_counter; @temp_counter += 1
          vname = "__pv#{vt}"
          emit_ref_temp_decl(vname, hv.resolved_type)
          gen_expr(hv)
          emit('; ')
        end
        emit("__zn_hash_set(__t#{t}, ")
        if fresh_key then emit_box_call(kname, hk.resolved_type) else gen_box_expr(hk) end
        emit(', ')
        if fresh_val then emit_box_call(vname, hv.resolved_type) else gen_box_expr(hv) end
        emit('); ')
        if fresh_key; emit_release_call(kname, hk.resolved_type); emit('; '); end
        if fresh_val; emit_release_call(vname, hv.resolved_type); emit('; '); end
      end
      emit("__t#{t}; })")
    end

    def gen_typed_empty_array_expr(expr)
      t = @temp_counter; @temp_counter += 1
      emit("({ ZnArray *__t#{t} = __zn_arr_alloc(0")
      emit_arr_callbacks(expr.resolved_type&.elem)
      emit("); __t#{t}; })")
    end

    def gen_typed_empty_hash_expr(expr)
      t = @temp_counter; @temp_counter += 1
      emit("({ ZnHash *__t#{t} = __zn_hash_alloc(8")
      emit_hash_callbacks(expr.resolved_type&.key, expr.resolved_type&.elem)
      emit("); __t#{t}; })")
    end

    def gen_optional_check_expr(expr)
      operand = expr.operand
      ot = operand.resolved_type&.kind || TK_UNKNOWN
      if ref_type?(ot)
        emit('('); gen_expr(operand); emit(' != NULL)')
      else
        if operand.is_a?(AST::Ident)
          emit("(#{operand.name}._has)")
        else
          emit('('); gen_expr(operand); emit('._has)')
        end
      end
    end

    def gen_if_expr(expr)
      rt = expr.resolved_type&.kind || TK_UNKNOWN
      return if rt == TK_UNKNOWN || rt == TK_VOID

      t = @temp_counter; @temp_counter += 1

      # Optional if-without-else
      is_opt = expr.resolved_type&.is_optional
      if is_opt && !expr.else_b
        gen_optional_if_expr(expr, t, rt)
        return
      end

      # Check for type narrowing
      expr_narrowing = false
      expr_narrow_name = nil
      cond = expr.cond
      if cond.is_a?(AST::OptionalCheck) && cond.operand.is_a?(AST::Ident)
        operand = cond.operand
        if operand.resolved_type&.is_optional && !ref_type?(operand.resolved_type.kind)
          expr_narrowing = true
          expr_narrow_name = operand.name
        end
      end

      # Non-optional if/else expression
      if rt == TK_STRUCT && expr.resolved_type&.name
        emit("({ #{expr.resolved_type.name} __if_#{t}; ")
      elsif rt == TK_CLASS && expr.resolved_type&.name
        emit("({ #{expr.resolved_type.name} *__if_#{t}; ")
      else
        emit("({ #{type_to_c(rt)} __if_#{t}; ")
      end
      emit('if ('); gen_expr(expr.cond); emit(') { ')

      if expr.then_b.is_a?(AST::Block)
        @narrowed.push(expr_narrow_name) if expr_narrowing
        stmts = expr.then_b.stmts
        stmts[0...-1].each { |s| gen_stmt(s) }
        if stmts.last
          emit("__if_#{t} = "); gen_expr(stmts.last); emit('; ')
          emit_inline_retain(t, '__if_', stmts.last, expr.resolved_type)
        end
        @narrowed.pop if expr_narrowing
      end
      emit('} else { ')
      else_b = expr.else_b
      if else_b.is_a?(AST::If)
        emit("__if_#{t} = "); gen_expr(else_b); emit('; ')
        emit_inline_retain(t, '__if_', else_b, expr.resolved_type)
      elsif else_b.is_a?(AST::Block)
        stmts = else_b.stmts
        stmts[0...-1].each { |s| gen_stmt(s) }
        if stmts.last
          emit("__if_#{t} = "); gen_expr(stmts.last); emit('; ')
          emit_inline_retain(t, '__if_', stmts.last, expr.resolved_type)
        end
      end
      emit("} __if_#{t}; })")
    end

    def gen_optional_if_expr(expr, t, rt)
      opt = opt_type_for(rt)
      if opt
        emit("({ #{opt} __if_#{t}; ")
        emit('if ('); gen_expr(expr.cond); emit(') { ')
        if expr.then_b.is_a?(AST::Block)
          stmts = expr.then_b.stmts
          stmts[0...-1].each { |s| gen_stmt(s) }
          if stmts.last
            emit("__if_#{t}._has = true; __if_#{t}._val = ")
            gen_expr(stmts.last)
            emit('; ')
          end
        end
        emit("} else { __if_#{t}._has = false; } __if_#{t}; })")
      else
        if rt == TK_CLASS && expr.resolved_type&.name
          emit("({ #{expr.resolved_type.name} *__if_#{t} = NULL; ")
        else
          emit("({ #{type_to_c(rt)} __if_#{t} = NULL; ")
        end
        emit('if ('); gen_expr(expr.cond); emit(') { ')
        if expr.then_b.is_a?(AST::Block)
          stmts = expr.then_b.stmts
          stmts[0...-1].each { |s| gen_stmt(s) }
          if stmts.last
            emit("__if_#{t} = "); gen_expr(stmts.last); emit('; ')
          end
        end
        emit("} __if_#{t}; })")
      end
    end

    def gen_while_expr(expr)
      rt = expr.resolved_type&.kind || TK_UNKNOWN
      return if rt == TK_UNKNOWN || rt == TK_VOID

      t = @temp_counter; @temp_counter += 1
      saved_let = @loop_expr_temp
      saved_opt = @loop_expr_optional
      @loop_expr_temp = t
      @loop_expr_type = rt
      is_opt = expr.resolved_type&.is_optional
      @loop_expr_optional = is_opt

      if is_opt
        opt = opt_type_for(rt)
        if opt
          emit("({ #{opt} __loop_#{t}; __loop_#{t}._has = false; ")
        elsif rt == TK_CLASS && expr.resolved_type&.name
          emit("({ #{expr.resolved_type.name} *__loop_#{t} = NULL; ")
        else
          emit("({ #{type_to_c(rt)} __loop_#{t} = NULL; ")
        end
      else
        if rt == TK_CLASS && expr.resolved_type&.name
          emit("({ #{expr.resolved_type.name} *__loop_#{t} = NULL; ")
        elsif ref_type?(rt)
          emit("({ #{type_to_c(rt)} __loop_#{t} = NULL; ")
        else
          emit("({ #{type_to_c(rt)} __loop_#{t}; ")
        end
      end

      emit('while ('); gen_expr(expr.cond); emit(') ')
      gen_block_with_scope(expr.body, true)
      emit(" __loop_#{t}; })")
      @loop_expr_temp = saved_let
      @loop_expr_optional = saved_opt
    end

    def gen_for_expr(expr)
      rt = expr.resolved_type&.kind || TK_UNKNOWN
      return if rt == TK_UNKNOWN || rt == TK_VOID

      t = @temp_counter; @temp_counter += 1
      saved_let = @loop_expr_temp
      saved_opt = @loop_expr_optional
      @loop_expr_temp = t
      @loop_expr_type = rt
      @loop_expr_optional = true

      opt = opt_type_for(rt)
      if opt
        emit("({ #{opt} __loop_#{t}; __loop_#{t}._has = false; ")
      elsif rt == TK_CLASS && expr.resolved_type&.name
        emit("({ #{expr.resolved_type.name} *__loop_#{t} = NULL; ")
      else
        emit("({ #{type_to_c(rt)} __loop_#{t} = NULL; ")
      end

      gen_for_header(expr)
      gen_block_with_scope(expr.body, true)
      emit(" __loop_#{t}; })")
      @loop_expr_temp = saved_let
      @loop_expr_optional = saved_opt
    end

    # --- Statement generation ---

    def gen_stmt(node)
      return unless node

      emit_indent

      case node
      when AST::Decl
        gen_decl_stmt(node)
      when AST::If
        gen_if_stmt(node)
      when AST::While
        emit('while ('); gen_expr(node.cond); emit(') ')
        gen_block_with_scope(node.body, true)
        emit("\n")
      when AST::For
        gen_for_header(node)
        gen_block_with_scope(node.body, true)
        emit("\n")
      when AST::Break
        gen_break_stmt(node)
      when AST::Continue
        gen_continue_stmt(node)
      when AST::Return
        gen_return_stmt(node)
      when AST::Assign
        gen_assign_stmt(node)
      when AST::FuncDef
        # skip - handled at top level
      else
        gen_expr(node)
        emit(";\n")
      end
    end

    def gen_decl_stmt(node)
      name = node.name
      value = node.value
      is_const = node.is_const
      cq = is_const ? 'const ' : ''
      vt = value.resolved_type
      t = vt&.kind || TK_UNKNOWN
      val_is_optional = vt&.is_optional

      if val_is_optional && opt_type_for(t)
        emit("#{cq}#{opt_type_for(t)} #{name} = ")
      elsif t == TK_CLASS && vt.name
        if is_const
          emit("#{vt.name} *const #{name} = ")
        else
          emit("#{vt.name} *#{name} = ")
        end
      elsif t == TK_STRUCT && vt.name
        emit("#{cq}#{vt.name} #{name} = ")
      elsif [TK_STRING, TK_ARRAY, TK_HASH].include?(t)
        emit("#{type_to_c(t)} #{name} = ")
      else
        emit("#{cq}#{type_to_c(t)} #{name} = ")
      end
      gen_expr(value)
      emit(";\n")
      emit_retain(name, value, vt)
      scope_track_ref(name, vt)
    end

    def gen_if_stmt(node)
      # Check for type narrowing
      cond = node.cond
      narrowing = false
      narrow_name = nil
      if cond.is_a?(AST::OptionalCheck) && cond.operand.is_a?(AST::Ident)
        operand = cond.operand
        if operand.resolved_type&.is_optional && !ref_type?(operand.resolved_type.kind)
          narrowing = true
          narrow_name = operand.name
        end
      end

      emit('if ('); gen_expr(cond); emit(') ')

      if narrowing && node.then_b.is_a?(AST::Block)
        @narrowed.push(narrow_name)
        gen_block(node.then_b)
        @narrowed.pop
      else
        gen_block(node.then_b)
      end

      if node.else_b
        emit(' else ')
        if node.else_b.is_a?(AST::If)
          gen_stmt(node.else_b)
        else
          gen_block(node.else_b)
          emit("\n")
        end
      else
        emit("\n")
      end
    end

    def gen_break_stmt(node)
      loop_scope = find_loop_scope
      if loop_scope
        s = @scope
        while s
          s.ref_vars.each { |v| emit_var_release(v) }
          break if s.equal?(loop_scope)
          s = s.parent
        end
        emit_indent
      end

      if @loop_expr_temp >= 0 && node.value
        gen_loop_value_assign(node.value)
      end
      emit("break;\n")
    end

    def gen_continue_stmt(node)
      loop_scope = find_loop_scope
      if loop_scope
        s = @scope
        while s
          s.ref_vars.each { |v| emit_var_release(v) }
          break if s.equal?(loop_scope)
          s = s.parent
        end
        emit_indent
      end

      if @loop_expr_temp >= 0 && node.value
        gen_loop_value_assign(node.value)
      end
      emit("continue;\n")
    end

    def gen_loop_value_assign(bv)
      is_opt = @loop_expr_optional && opt_type_for(@loop_expr_type)
      if bv.resolved_type && ref_type?(bv.resolved_type.kind)
        t_val = @temp_counter; @temp_counter += 1
        tname = "__t#{t_val}"
        emit_ref_temp_decl(tname, bv.resolved_type)
        gen_expr(bv)
        emit(";\n")
        emit_indent
        unless bv.is_fresh_alloc
          emit_retain_call(tname, bv.resolved_type)
          emit(";\n")
          emit_indent
        end
        lbuf = is_opt ? "__loop_#{@loop_expr_temp}._val" : "__loop_#{@loop_expr_temp}"
        emit_release_call(lbuf, bv.resolved_type)
        emit(";\n")
        emit_indent
        if is_opt
          emit("__loop_#{@loop_expr_temp}._has = true; __loop_#{@loop_expr_temp}._val = #{tname};\n")
        else
          emit("__loop_#{@loop_expr_temp} = #{tname};\n")
        end
      else
        if is_opt
          emit("__loop_#{@loop_expr_temp}._has = true; __loop_#{@loop_expr_temp}._val = ")
        else
          emit("__loop_#{@loop_expr_temp} = ")
        end
        gen_expr(bv)
        emit(";\n")
      end
      emit_indent
    end

    def gen_return_stmt(node)
      rv = node.value
      if !rv
        emit_all_scope_releases
        emit_indent
        emit("return;\n")
      else
        rt = rv.resolved_type
        rk = rt&.kind || TK_UNKNOWN
        if rk == TK_VOID || rk == TK_UNKNOWN
          emit_all_scope_releases
          emit_indent
          emit('return '); gen_expr(rv); emit(";\n")
        else
          t = @temp_counter; @temp_counter += 1
          if rk == TK_CLASS && rt.name
            emit("#{rt.name} *__ret#{t} = ")
          elsif rk == TK_STRUCT && rt.name
            emit("#{rt.name} __ret#{t} = ")
          else
            emit("#{type_to_c(rk)} __ret#{t} = ")
          end
          gen_expr(rv)
          emit(";\n")
          emit_retain("__ret#{t}", rv, rt)
          emit_all_scope_releases
          emit_indent
          emit("return __ret#{t};\n")
        end
      end
    end

    def gen_assign_stmt(node)
      tgt = node.target
      val = node.value
      val_kind = val.resolved_type&.kind || TK_UNKNOWN

      if tgt.is_a?(AST::Index)
        gen_index_assign_stmt(tgt, val)
        return
      end

      if tgt.is_a?(AST::FieldAccess)
        gen_field_assign_stmt(tgt, val)
        return
      end

      # Simple variable assignment
      name = tgt.name
      vtype = val.resolved_type
      if vtype && ref_type?(val_kind)
        if val_kind == TK_CLASS
          # Class: retain-before-release
          if !val.is_fresh_alloc
            t = @temp_counter; @temp_counter += 1
            emit("struct #{vtype.name} *__t#{t} = ")
            gen_expr(val)
            emit(";\n")
            emit_indent
            emit_retain_open(vtype)
            emit("__t#{t});\n")
            emit_indent
            emit_release_call(name, vtype)
            emit(";\n")
            emit_indent
            emit("#{name} = __t#{t};\n")
          else
            emit_release_call(name, vtype)
            emit(";\n")
            emit_indent
            emit("#{name} = "); gen_expr(val); emit(";\n")
          end
        else
          # String/Array/Hash: release-assign-retain
          emit_release_call(name, vtype)
          emit(";\n")
          emit_indent
          emit("#{name} = "); gen_expr(val); emit(";\n")
          unless val.is_fresh_alloc
            emit_indent
            emit_retain_call(name, vtype)
            emit(";\n")
          end
        end
      else
        gen_expr(node)
        emit(";\n")
      end
    end

    def gen_index_assign_stmt(tgt, val)
      obj = tgt.object
      obj_kind = obj.resolved_type&.kind || TK_UNKNOWN

      if obj_kind == TK_ARRAY
        fresh_val = val.resolved_type && ref_type?(val.resolved_type.kind) && val.is_fresh_alloc
        if fresh_val
          pt = @temp_counter; @temp_counter += 1
          pname = "__ps#{pt}"
          emit('{ ')
          emit_ref_temp_decl(pname, val.resolved_type)
          gen_expr(val)
          emit('; __zn_arr_set(')
          gen_expr(obj)
          emit(', ')
          gen_expr(tgt.index)
          emit(', ')
          emit_box_call(pname, val.resolved_type)
          emit('); ')
          emit_release_call(pname, val.resolved_type)
          emit("; }\n")
        else
          emit('__zn_arr_set(')
          gen_expr(obj)
          emit(', ')
          gen_expr(tgt.index)
          emit(', ')
          gen_box_expr(val)
          emit(");\n")
        end
      elsif obj_kind == TK_HASH
        fresh_val = val.resolved_type && ref_type?(val.resolved_type.kind) && val.is_fresh_alloc
        if fresh_val
          pt = @temp_counter; @temp_counter += 1
          pname = "__ps#{pt}"
          emit('{ ')
          emit_ref_temp_decl(pname, val.resolved_type)
          gen_expr(val)
          emit('; __zn_hash_set(')
          gen_expr(obj)
          emit(', ')
          gen_box_expr(tgt.index)
          emit(', ')
          emit_box_call(pname, val.resolved_type)
          emit('); ')
          emit_release_call(pname, val.resolved_type)
          emit("; }\n")
        else
          emit('__zn_hash_set(')
          gen_expr(obj)
          emit(', ')
          gen_box_expr(tgt.index)
          emit(', ')
          gen_box_expr(val)
          emit(");\n")
        end
      end
    end

    def gen_field_assign_stmt(tgt, val)
      obj = tgt.object
      field = tgt.field
      obj_sn = obj.resolved_type&.name
      obj_kind = obj.resolved_type&.kind || TK_UNKNOWN

      fd = nil
      if (obj_kind == TK_STRUCT || obj_kind == TK_CLASS) && obj_sn
        sd = @sem.lookup_struct(obj_sn)
        if sd
          f = sd.fields
          while f
            if f.name == field then fd = f; break end
            f = f.next
          end
        end
      end

      if fd&.is_weak
        gen_expr(obj)
        emit("->#{field} = ")
        gen_expr(val)
        emit(";\n")
      elsif fd && ref_type?(fd.type.kind) && obj_kind == TK_CLASS
        # Class ref-type field
        t_obj = @temp_counter; @temp_counter += 1
        t_val = @temp_counter; @temp_counter += 1
        emit("struct #{obj_sn} *__t#{t_obj} = ")
        gen_expr(obj)
        emit(";\n")
        emit_indent
        tname = "__t#{t_val}"
        emit_ref_temp_decl(tname, fd.type)
        gen_expr(val)
        emit(";\n")
        emit_indent
        if !val || !val.is_fresh_alloc
          emit_retain_open(fd.type)
          emit("__t#{t_val});\n")
          emit_indent
        end
        emit_release_open(fd.type)
        emit("__t#{t_obj}->#{field});\n")
        emit_indent
        emit("__t#{t_obj}->#{field} = __t#{t_val};\n")
      elsif fd && ref_type?(fd.type.kind) && obj_kind == TK_STRUCT
        # Struct ref-type field
        t_val = @temp_counter; @temp_counter += 1
        tname = "__t#{t_val}"
        emit_ref_temp_decl(tname, fd.type)
        gen_expr(val)
        emit(";\n")
        emit_indent
        if !val || !val.is_fresh_alloc
          emit_retain_open(fd.type)
          emit("__t#{t_val});\n")
          emit_indent
        end
        emit_release_open(fd.type)
        gen_expr(obj)
        emit(".#{field});\n")
        emit_indent
        gen_expr(obj)
        emit(".#{field} = __t#{t_val};\n")
      else
        gen_expr(node_for_assign(tgt, val))
        emit(";\n")
      end
    end

    def gen_stmts(stmts)
      stmts.each do |s|
        next if s.is_a?(AST::FuncDef)
        emit_line(s.line)
        gen_stmt(s)
      end
    end

    # Generate function prototype
    def gen_func_proto(func, to_header)
      sym = @sem.lookup(func.name)
      ret_type = sym&.type&.kind || TK_VOID

      out = to_header ? @h_file : @c_file

      ret_is_class = false
      if func.name == 'main'
        ret_str = 'int'
      elsif ret_type == TK_CLASS && sym&.type&.name
        ret_is_class = true
        ret_str = sym.type.name
      elsif ret_type == TK_STRUCT && sym&.type&.name
        ret_str = sym.type.name
      else
        ret_str = type_to_c(ret_type)
      end

      if ret_is_class
        out.write("#{ret_str} *#{func.name}(")
      else
        out.write("#{ret_str} #{func.name}(")
      end

      first = true
      func.params.each do |p|
        out.write(', ') unless first
        ti = p.type_info
        opt = ti.is_optional ? opt_type_for(ti.kind) : nil
        if opt
          out.write("const #{opt} #{p.name}")
        elsif ti.kind == TK_CLASS && ti.name
          out.write("#{ti.name} *#{p.name}")
        elsif ti.kind == TK_STRUCT && ti.name
          psd = @sem.lookup_struct(ti.name)
          if psd&.is_class
            out.write("#{ti.name} *#{p.name}")
          else
            out.write("const #{ti.name} #{p.name}")
          end
        else
          out.write("const #{type_to_c(ti.kind)} #{p.name}")
        end
        first = false
      end
      out.write('void') if first
      out.write(')')
    end

    # Generate function body with implicit return
    def gen_func_body(block, ret_type)
      return unless block.is_a?(AST::Block)

      emit("{\n")
      @indent_level += 1
      push_scope(false)

      stmts = block.stmts
      if stmts.empty?
        pop_scope
        @indent_level -= 1
        emit_indent
        emit('}')
        return
      end

      last = stmts.last
      stmts[0...-1].each { |s| gen_stmt(s) }

      if last
        last_kind = last.resolved_type&.kind || TK_UNKNOWN
        if last.is_a?(AST::Return)
          gen_stmt(last)
        elsif ret_type == TK_VOID || last_kind == TK_VOID
          gen_stmt(last)
          emit_scope_releases
        elsif ref_type?(ret_type)
          gen_ref_implicit_return(last, ret_type)
        elsif ret_type == TK_STRUCT && last.resolved_type&.name
          t = @temp_counter; @temp_counter += 1
          emit_indent
          emit("#{last.resolved_type.name} __ret#{t} = ")
          gen_expr(last)
          emit(";\n")
          emit_scope_releases
          emit_indent
          emit("return __ret#{t};\n")
        else
          t = @temp_counter; @temp_counter += 1
          emit_indent
          emit("#{type_to_c(ret_type)} __ret#{t} = ")
          gen_expr(last)
          emit(";\n")
          emit_scope_releases
          emit_indent
          emit("return __ret#{t};\n")
        end
      else
        emit_scope_releases
      end

      pop_scope
      @indent_level -= 1
      emit_indent
      emit('}')
    end

    def gen_ref_implicit_return(last, ret_type)
      t = @temp_counter; @temp_counter += 1
      emit_indent
      if ret_type == TK_CLASS && last.resolved_type&.name
        emit("#{last.resolved_type.name} *__ret#{t} = ")
      else
        emit("#{type_to_c(ret_type)} __ret#{t} = ")
      end
      gen_expr(last)
      emit(";\n")
      unless last.is_fresh_alloc
        emit_indent
        if ret_type == TK_STRING
          emit("__zn_str_retain(__ret#{t});\n")
        elsif ret_type == TK_ARRAY
          emit("__zn_arr_retain(__ret#{t});\n")
        elsif ret_type == TK_HASH
          emit("__zn_hash_retain(__ret#{t});\n")
        elsif ret_type == TK_CLASS && last.resolved_type&.name
          emit("__#{last.resolved_type.name}_retain(__ret#{t});\n")
        end
      end
      emit_scope_releases
      emit_indent
      emit("return __ret#{t};\n")
    end

    def gen_func_def(func)
      gen_func_proto(func, true)
      emit_header(";\n")

      sym = @sem.lookup(func.name)
      ret_type = sym&.type&.kind || TK_VOID

      gen_func_proto(func, false)
      emit(' ')
      gen_func_body(func.body, ret_type)
      emit("\n\n")
    end

    private

    def find_named_arg(args, name)
      args.each do |a|
        return a.value if a.is_a?(AST::NamedArg) && a.name == name
      end
      nil
    end

    # Helper for field assign when no special ARC handling needed
    def node_for_assign(tgt, val)
      # Create a synthetic assign for gen_expr
      AST::Assign.new(tgt, val)
    end
  end
end
