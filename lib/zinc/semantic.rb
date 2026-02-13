# frozen_string_literal: true

module Zinc
  # Struct field definition (for struct registry)
  StructFieldDef = Struct.new(:name, :type, :has_default, :is_const, :is_weak, :default_value, :next) do
    def initialize(name = nil, type = nil, has_default = false, is_const = false, is_weak = false, default_value = nil, next_field = nil)
      super(name, type, has_default, is_const, is_weak, default_value, next_field)
    end
  end

  # Struct definition (for struct registry)
  StructDef = Struct.new(:name, :fields, :field_count, :is_class) do
    def initialize(name = nil, fields = nil, field_count = 0, is_class = false)
      super(name, fields, field_count, is_class)
    end

    def lookup_field(fname)
      f = fields
      while f
        return f if f.name == fname
        f = f.next
      end
      nil
    end
  end

  # Symbol table entry
  Symbol = Struct.new(:name, :type, :is_const, :is_function, :is_extern, :param_count, :param_types) do
    def initialize(name = nil, type = nil, is_const = false, is_function = false, is_extern = false, param_count = 0, param_types = nil)
      super(name, type, is_const, is_function, is_extern, param_count, param_types)
    end
  end

  class Semantic
    attr_reader :error_count, :struct_defs

    def initialize
      @scopes = [{}]  # stack of hashes (name -> Symbol)
      @struct_defs = {}  # name -> StructDef
      @error_count = 0
      @in_loop = 0
      @in_function = false
      @current_func_return_type = nil
      @loop_result_type = nil
      @loop_result_set = false
    end

    def analyze(root)
      return 1 unless root.is_a?(AST::Program)
      analyze_stmts(root.stmts)
      @error_count
    end

    def lookup(name)
      @scopes.reverse_each do |scope|
        return scope[name] if scope.key?(name)
      end
      nil
    end

    def lookup_struct(name)
      @struct_defs[name]
    end

    def get_expr_type(expr)
      void_type = Type.new(TK_VOID)
      return void_type unless expr

      return expr.resolved_type if expr.resolved_type

      result = TK_UNKNOWN

      case expr
      when AST::IntLit    then result = TK_INT
      when AST::FloatLit  then result = TK_FLOAT
      when AST::StringLit then result = TK_STRING
      when AST::BoolLit   then result = TK_BOOL
      when AST::CharLit   then result = TK_CHAR
      when AST::Ident
        sym = lookup(expr.name)
        if !sym
          sem_error(expr.line, "undefined variable '#{expr.name}'")
          result = TK_UNKNOWN
        else
          expr.resolved_type = sym.type.clone
          return expr.resolved_type
        end
      when AST::BinOp
        left = get_expr_type(expr.left).kind
        right = get_expr_type(expr.right).kind
        op = expr.op
        if [Op::EQ, Op::NE, Op::LT, Op::GT, Op::LE, Op::GE].include?(op)
          result = TK_BOOL
        elsif op == Op::AND || op == Op::OR
          result = TK_BOOL
        elsif op == Op::ADD && (left == TK_STRING || right == TK_STRING)
          result = TK_STRING
          expr.is_fresh_alloc = true
        elsif left == TK_FLOAT || right == TK_FLOAT
          result = TK_FLOAT
        else
          result = TK_INT
        end
      when AST::UnaryOp
        if expr.op == Op::NOT
          result = TK_BOOL
        else
          result = get_expr_type(expr.operand).kind
        end
      when AST::Call
        if expr.name == 'print'
          result = TK_VOID
        else
          sym = lookup(expr.name)
          if !sym
            sem_error(expr.line, "undefined function '#{expr.name}'")
            result = TK_UNKNOWN
          elsif !sym.is_function
            sem_error(expr.line, "'#{expr.name}' is not a function")
            result = TK_UNKNOWN
          else
            expr.resolved_type = sym.type.clone
            rk = sym.type.kind
            if [TK_STRING, TK_CLASS, TK_ARRAY, TK_HASH].include?(rk)
              expr.is_fresh_alloc = true
            end
            return expr.resolved_type
          end
        end
      when AST::Assign
        result = get_expr_type(expr.value).kind
      when AST::CompoundAssign
        result = get_expr_type(expr.value).kind
      when AST::IncDec
        result = TK_INT
      when AST::FieldAccess
        result = expr.resolved_type ? expr.resolved_type.kind : TK_UNKNOWN
      when AST::Index
        result = expr.resolved_type ? expr.resolved_type.kind : TK_UNKNOWN
      when AST::ArrayLiteral, AST::TypedEmptyArray
        result = TK_ARRAY
      when AST::HashLiteral, AST::TypedEmptyHash
        result = TK_HASH
      when AST::OptionalCheck
        result = TK_BOOL
      when AST::Tuple, AST::ObjectLiteral
        result = expr.resolved_type ? expr.resolved_type.kind : TK_STRUCT
      when AST::NamedArg
        result = get_expr_type(expr.value).kind
      when AST::If, AST::While, AST::For
        result = expr.resolved_type ? expr.resolved_type.kind : TK_UNKNOWN
      when AST::Break
        result = get_expr_type(expr.value).kind if expr.value
      when AST::Continue
        result = get_expr_type(expr.value).kind if expr.value
      end

      if !expr.resolved_type
        expr.resolved_type = Type.new(result)
      else
        expr.resolved_type.kind = result
      end
      expr.resolved_type
    end

    private

    def ref_type?(kind)
      [TK_STRING, TK_CLASS, TK_ARRAY, TK_HASH].include?(kind)
    end

    def sem_error(line, msg)
      $stderr.puts "Semantic error at line #{line}: #{msg}"
      @error_count += 1
    end

    def push_scope
      @scopes.push({})
    end

    def pop_scope
      @scopes.pop
    end

    def lookup_local(name)
      @scopes.last[name]
    end

    def add_symbol(line, name, type, is_const)
      if lookup_local(name)
        sem_error(line, "variable '#{name}' already declared in this scope")
        return nil
      end
      sym = Symbol.new(name, type.clone, is_const)
      @scopes.last[name] = sym
      sym
    end

    def add_function(line, name, return_type, param_count, param_types, is_extern)
      if lookup_local(name)
        sem_error(line, "function '#{name}' already declared in this scope")
        return nil
      end
      sym = Symbol.new(name, return_type.clone, false, true, is_extern, param_count,
                        param_types&.map(&:clone))
      @scopes.last[name] = sym
      sym
    end

    def add_extern_var(line, name, type, is_const)
      if lookup_local(name)
        sem_error(line, "symbol '#{name}' already declared in this scope")
        return nil
      end
      sym = Symbol.new(name, type.clone, is_const, false, true)
      @scopes.last[name] = sym
      sym
    end

    def register_struct(name, is_class)
      sd = StructDef.new(name, nil, 0, is_class)
      @struct_defs[name] = sd
      sd
    end

    TYPE_KIND_SUFFIX = {
      TK_INT => 'int', TK_FLOAT => 'float', TK_STRING => 'str',
      TK_BOOL => 'bool', TK_CHAR => 'char',
      TK_ARRAY => 'arr', TK_HASH => 'hash',
    }.freeze

    TYPE_KIND_NAME = {
      TK_INT => 'int', TK_FLOAT => 'float', TK_STRING => 'string',
      TK_BOOL => 'bool', TK_CHAR => 'char', TK_VOID => 'void',
      TK_STRUCT => 'struct', TK_CLASS => 'class',
      TK_ARRAY => 'array', TK_HASH => 'hash',
    }.freeze

    def type_kind_suffix(t)
      TYPE_KIND_SUFFIX[t] || 'unk'
    end

    def type_kind_name(t)
      TYPE_KIND_NAME[t] || 'unknown'
    end

    def is_always_true(expr)
      return false unless expr
      return true if expr.is_a?(AST::BoolLit) && expr.value
      if expr.is_a?(AST::UnaryOp) && expr.op == Op::NOT
        inner = expr.operand
        return true if inner.is_a?(AST::BoolLit) && !inner.value
      end
      false
    end

    def is_definitely_void(expr)
      return false unless expr
      return false unless expr.is_a?(AST::Call)
      sym = lookup(expr.name)
      sym && sym.is_function && sym.is_extern && sym.type.kind == TK_VOID
    end

    def check_not_void(line, expr, context)
      if is_definitely_void(expr)
        sem_error(line, "cannot use void expression #{context}")
      end
    end

    def get_suffix(kind, resolved_type)
      if (kind == TK_STRUCT || kind == TK_CLASS) && resolved_type&.name
        resolved_type.name
      elsif kind == TK_ARRAY
        'arr'
      elsif kind == TK_HASH
        'hash'
      else
        type_kind_suffix(kind)
      end
    end

    # Build a linked list of StructFieldDef from fields array, returning [head, count]
    def build_field_defs(fields_data)
      head = nil
      tail = nil
      count = 0
      fields_data.each do |fd_data|
        fd = StructFieldDef.new(fd_data[:name], fd_data[:type], fd_data[:has_default],
                                 fd_data[:is_const], fd_data[:is_weak], fd_data[:default_value])
        if tail
          tail.next = fd
        else
          head = fd
        end
        tail = fd
        count += 1
      end
      [head, count]
    end

    # Resolve TypeInfo with object/tuple fields into a registered struct/class
    def resolve_type_info(ti)
      return unless ti&.fields

      if ti.is_tuple
        # Recursively resolve nested types
        f = ti.fields
        while f
          resolve_type_info(f.type)
          f = f.next
        end

        # Assign _N names to positional fields
        idx = 0
        has_named = false
        has_positional = false
        f = ti.fields
        while f
          if f.name
            has_named = true
          else
            has_positional = true
            f.name = "_#{idx}"
          end
          idx += 1
          f = f.next
        end

        # Build canonical name
        buf = '__ZnTuple'
        f = ti.fields
        while f
          suffix = if (f.type.kind == TK_STRUCT || f.type.kind == TK_CLASS) && f.type.name
                     f.type.name
                   else
                     type_kind_suffix(f.type.kind)
                   end
          if has_named && !has_positional
            buf += "_#{f.name}_#{suffix}"
          else
            buf += "_#{suffix}"
          end
          f = f.next
        end

        # Register anonymous struct if not already present
        unless lookup_struct(buf)
          fields_data = []
          f = ti.fields
          while f
            fields_data << { name: f.name, type: f.type.to_type, is_const: false }
            f = f.next
          end
          sd = register_struct(buf, false)
          head, count = build_field_defs(fields_data)
          sd.fields = head
          sd.field_count = count
        end

        ti.kind = TK_STRUCT
        ti.name = buf
        return
      end

      return unless ti.is_object

      # Recursively resolve nested object types
      f = ti.fields
      while f
        resolve_type_info(f.type)
        f = f.next
      end

      # Build canonical name
      buf = '__obj'
      f = ti.fields
      while f
        suffix = if (f.type.kind == TK_STRUCT || f.type.kind == TK_CLASS) && f.type.name
                   f.type.name
                 else
                   type_kind_suffix(f.type.kind)
                 end
        buf += "_#{f.name}_#{suffix}"
        f = f.next
      end

      # Register anonymous class if not already present
      unless lookup_struct(buf)
        sd = register_struct(buf, true)
        fields_data = []
        f = ti.fields
        while f
          fields_data << { name: f.name, type: f.type.to_type }
          f = f.next
        end
        head, count = build_field_defs(fields_data)
        sd.fields = head
        sd.field_count = count
      end

      ti.kind = TK_CLASS
      ti.name = buf
    end

    # Check if an expression is a valid lvalue, return StructFieldDef if field access
    def check_lvalue(tgt, line, verb)
      case tgt
      when AST::Ident
        sym = lookup(tgt.name)
        if !sym
          sem_error(line, "undefined variable '#{tgt.name}'")
        elsif sym.is_const
          sem_error(line, "cannot #{verb} constant '#{tgt.name}'")
        elsif sym.is_extern
          sem_error(line, "cannot #{verb} extern '#{tgt.name}'")
        end
      when AST::FieldAccess
        obj = tgt.object
        obj_kind = obj.resolved_type&.kind || TK_UNKNOWN
        obj_sn = obj.resolved_type&.name
        if (obj_kind == TK_STRUCT || obj_kind == TK_CLASS) && obj_sn
          fsd = lookup_struct(obj_sn)
          if fsd
            fd = fsd.lookup_field(tgt.field)
            if fd&.is_const
              sem_error(line, "cannot #{verb} immutable field '#{tgt.field}'")
            end
            if fd && obj_kind != TK_CLASS
              # Binding immutability for value types
              cur = obj
              while cur.is_a?(AST::FieldAccess)
                cur = cur.object
              end
              if cur.is_a?(AST::Ident)
                sym = lookup(cur.name)
                if sym&.is_const
                  sem_error(line, "cannot modify field of immutable variable '#{cur.name}'")
                end
              end
            end
            return fd
          end
        end
      when AST::Index
        obj_type = tgt.object.resolved_type&.kind || TK_UNKNOWN
        if obj_type == TK_STRING
          sem_error(line, "strings are immutable")
        end
      else
        sem_error(line, "invalid assignment target")
      end
      nil
    end

    def analyze_expr(expr)
      return unless expr

      case expr
      when AST::Ident
        sym = lookup(expr.name)
        unless sym
          sem_error(expr.line, "undefined variable '#{expr.name}'")
        end

      when AST::BinOp
        analyze_expr(expr.left)
        analyze_expr(expr.right)
        check_not_void(expr.line, expr.left, 'as operand')
        check_not_void(expr.line, expr.right, 'as operand')

      when AST::UnaryOp
        analyze_expr(expr.operand)
        check_not_void(expr.line, expr.operand, 'as operand')

      when AST::Assign
        analyze_expr(expr.target)
        analyze_expr(expr.value)
        check_not_void(expr.line, expr.value, 'in assignment')
        fd = check_lvalue(expr.target, expr.line, 'assign to')
        if fd
          expr.resolved_type = fd.type.clone
        end

      when AST::CompoundAssign
        analyze_expr(expr.target)
        analyze_expr(expr.value)
        check_not_void(expr.line, expr.value, 'in assignment')
        check_lvalue(expr.target, expr.line, 'assign to')

      when AST::IncDec
        analyze_expr(expr.target)
        check_lvalue(expr.target, expr.line, 'modify')

      when AST::Call
        analyze_call(expr)

      when AST::FieldAccess
        analyze_field_access(expr)

      when AST::Index
        analyze_index(expr)

      when AST::NamedArg
        analyze_expr(expr.value)
        get_expr_type(expr.value)

      when AST::Tuple
        analyze_tuple(expr)

      when AST::ObjectLiteral
        analyze_object_literal(expr)

      when AST::ArrayLiteral
        analyze_array_literal(expr)

      when AST::HashLiteral
        analyze_hash_literal(expr)

      when AST::TypedEmptyArray
        analyze_typed_empty_array(expr)

      when AST::TypedEmptyHash
        analyze_typed_empty_hash(expr)

      when AST::OptionalCheck
        analyze_optional_check(expr)

      when AST::If, AST::While, AST::For, AST::Break, AST::Continue
        analyze_stmt(expr)
      end

      # Ensure resolved_type is set
      get_expr_type(expr)
    end

    def analyze_call(expr)
      name = expr.name

      # Check if this is a struct instantiation
      sd = lookup_struct(name)
      if sd
        expr.is_struct_init = true

        # Validate named args match struct fields
        expr.args.each do |a|
          if a.is_a?(AST::NamedArg)
            fd = sd.lookup_field(a.name)
            unless fd
              sem_error(expr.line, "struct '#{name}' has no field '#{a.name}'")
            end
            analyze_expr(a.value)
            get_expr_type(a.value)
          else
            sem_error(expr.line, "struct '#{name}' requires named arguments")
            analyze_expr(a)
            get_expr_type(a)
          end
        end

        # Check all required fields are provided
        f = sd.fields
        while f
          if !f.has_default && !f.is_weak
            found = expr.args.any? { |a| a.is_a?(AST::NamedArg) && a.name == f.name }
            unless found
              sem_error(expr.line, "missing required field '#{f.name}' for struct '#{name}'")
            end
          end
          f = f.next
        end

        if !expr.resolved_type
          expr.resolved_type = Type.new(sd.is_class ? TK_CLASS : TK_STRUCT)
        else
          expr.resolved_type.kind = sd.is_class ? TK_CLASS : TK_STRUCT
        end
        expr.resolved_type.name = name
        expr.is_fresh_alloc = true if sd.is_class
        return
      end

      # Built-in print
      if name == 'print'
        expr.args.each do |a|
          analyze_expr(a)
          get_expr_type(a)
        end
        argc = expr.args.size
        if argc != 1
          sem_error(expr.line, "print expects exactly 1 argument, got #{argc}")
        else
          arg = expr.args[0]
          ak = arg.resolved_type&.kind || TK_UNKNOWN
          if ak != TK_STRING && ak != TK_UNKNOWN
            sem_error(expr.line, "print argument must be a String")
          end
        end
        if !expr.resolved_type
          expr.resolved_type = Type.new(TK_VOID)
        else
          expr.resolved_type.kind = TK_VOID
        end
        return
      end

      sym = lookup(name)
      if !sym
        sem_error(expr.line, "undefined function '#{name}'")
      elsif !sym.is_function
        sem_error(expr.line, "'#{name}' is not a function")
      end

      # Analyze arguments
      arg_count = 0
      expr.args.each do |a|
        analyze_expr(a)
        get_expr_type(a)
        check_not_void(expr.line, a, 'as function argument')
        arg_count += 1
      end

      # Arity and type checking
      if sym&.is_function && sym.param_count >= 0
        if arg_count != sym.param_count
          sem_error(expr.line, "function '#{name}' expects #{sym.param_count} argument(s), got #{arg_count}")
        elsif sym.param_types
          expr.args.each_with_index do |a, i|
            break unless i < sym.param_types.size
            expected = sym.param_types[i].kind
            actual = a.resolved_type&.kind || TK_UNKNOWN
            if actual != TK_UNKNOWN && expected != TK_UNKNOWN && actual != expected
              sem_error(expr.line, "argument #{i + 1} of '#{name}' expects #{type_kind_name(expected)}, got #{type_kind_name(actual)}")
            end
          end
        end
      end
    end

    def analyze_field_access(expr)
      analyze_expr(expr.object)
      get_expr_type(expr.object)
      obj = expr.object
      field = expr.field
      obj_kind = obj.resolved_type&.kind || TK_UNKNOWN

      # String .length
      if obj_kind == TK_STRING && field == 'length'
        if !expr.resolved_type then expr.resolved_type = Type.new(TK_INT)
        else expr.resolved_type.kind = TK_INT end
        return
      end
      if obj_kind == TK_STRING
        sem_error(expr.line, "string has no field '#{field}'")
        return
      end

      # Array/Hash .length
      if (obj_kind == TK_ARRAY || obj_kind == TK_HASH) && field == 'length'
        if !expr.resolved_type then expr.resolved_type = Type.new(TK_INT)
        else expr.resolved_type.kind = TK_INT end
        return
      end

      # Struct/class field access
      obj_struct_name = obj.resolved_type&.name
      if (obj_kind != TK_STRUCT && obj_kind != TK_CLASS) || !obj_struct_name
        if obj_kind != TK_UNKNOWN
          sem_error(expr.line, "field access on non-struct type")
        end
        return
      end

      sd = lookup_struct(obj_struct_name)
      unless sd
        sem_error(expr.line, "undefined struct type '#{obj_struct_name}'")
        return
      end

      # Reject ._N syntax on tuples
      if obj_struct_name.start_with?('__ZnTuple') && !expr.is_dot_int &&
         field[0] == '_' && field[1] && field[1] >= '0' && field[1] <= '9'
        sem_error(expr.line, "use '.#{field[1..]}' syntax instead of '.#{field}' for tuple field access")
        return
      end

      fd = sd.lookup_field(field)
      unless fd
        sem_error(expr.line, "struct '#{obj_struct_name}' has no field '#{field}'")
        return
      end

      expr.resolved_type = fd.type.clone
    end

    def analyze_index(expr)
      analyze_expr(expr.object)
      analyze_expr(expr.index)
      obj_type = get_expr_type(expr.object).kind
      idx_type = get_expr_type(expr.index).kind

      if obj_type == TK_ARRAY
        obj_t = expr.object.resolved_type
        expr.resolved_type = (obj_t&.elem) ? obj_t.elem.clone : Type.new(TK_UNKNOWN)
        if idx_type != TK_INT
          sem_error(expr.line, "array index must be an int")
        end
      elsif obj_type == TK_HASH
        obj_t = expr.object.resolved_type
        expr.resolved_type = (obj_t&.elem) ? obj_t.elem.clone : Type.new(TK_UNKNOWN)
      elsif obj_type == TK_STRING
        if !expr.resolved_type then expr.resolved_type = Type.new(TK_CHAR)
        else expr.resolved_type.kind = TK_CHAR end
        if idx_type != TK_INT && idx_type != TK_UNKNOWN
          sem_error(expr.line, "string index must be an integer")
        end
      elsif obj_type != TK_UNKNOWN
        sem_error(expr.line, "index operator requires an array, hash, or string")
      end
    end

    def analyze_tuple(expr)
      elems = expr.elements

      # Analyze all elements
      elems.each do |e|
        if e.is_a?(AST::NamedArg)
          analyze_expr(e.value)
          get_expr_type(e.value)
        else
          analyze_expr(e)
          get_expr_type(e)
        end
      end

      # Build canonical name
      canonical = '__ZnTuple'
      is_named = elems.first&.is_a?(AST::NamedArg)

      elems.each do |e|
        if is_named
          et = get_expr_type(e.value).kind
          rt = e.value.resolved_type
          suffix = get_suffix(et, rt)
          canonical += "_#{e.name}_#{suffix}"
        else
          et = get_expr_type(e).kind
          rt = e.resolved_type
          suffix = get_suffix(et, rt)
          canonical += "_#{suffix}"
        end
      end

      # Register anonymous type if needed
      unless lookup_struct(canonical)
        sd = register_struct(canonical, false)
        fields_data = []
        elems.each_with_index do |e, idx|
          if is_named
            tk = get_expr_type(e.value).kind
            rt = e.value.resolved_type
            t = Type.new(tk)
            t.name = rt.name.dup if (tk == TK_STRUCT || tk == TK_CLASS) && rt&.name
            t.elem = rt.elem.clone if tk == TK_ARRAY && rt&.elem
            if tk == TK_HASH && rt
              t.elem = rt.elem.clone if rt.elem
              t.key = rt.key.clone if rt.key
            end
            fields_data << { name: e.name, type: t, is_const: false }
          else
            tk = get_expr_type(e).kind
            rt = e.resolved_type
            t = Type.new(tk)
            t.name = rt.name.dup if (tk == TK_STRUCT || tk == TK_CLASS) && rt&.name
            t.elem = rt.elem.clone if tk == TK_ARRAY && rt&.elem
            if tk == TK_HASH && rt
              t.elem = rt.elem.clone if rt.elem
              t.key = rt.key.clone if rt.key
            end
            fields_data << { name: "_#{idx}", type: t, is_const: false }
          end
        end
        head, count = build_field_defs(fields_data)
        sd.fields = head
        sd.field_count = count
      end

      if !expr.resolved_type then expr.resolved_type = Type.new(TK_STRUCT)
      else expr.resolved_type.kind = TK_STRUCT end
      expr.resolved_type.name = canonical
    end

    def analyze_object_literal(expr)
      fields = expr.fields

      # All fields are NamedArg (enforced by grammar)
      fields.each do |na|
        analyze_expr(na.value)
        get_expr_type(na.value)
      end

      # Build canonical name
      buf = '__obj'
      fields.each do |na|
        tk = na.value.resolved_type&.kind || TK_UNKNOWN
        suffix = get_suffix(tk, na.value.resolved_type)
        buf += "_#{na.name}_#{suffix}"
      end

      # Register anonymous class if needed
      unless lookup_struct(buf)
        sd = register_struct(buf, true)
        fields_data = []
        fields.each do |na|
          tk = na.value.resolved_type&.kind || TK_UNKNOWN
          t = Type.new(tk)
          rt = na.value.resolved_type
          t.name = rt.name.dup if (tk == TK_STRUCT || tk == TK_CLASS) && rt&.name
          t.elem = rt.elem.clone if tk == TK_ARRAY && rt&.elem
          if tk == TK_HASH && rt
            t.elem = rt.elem.clone if rt.elem
            t.key = rt.key.clone if rt.key
          end
          fields_data << { name: na.name, type: t }
        end
        head, count = build_field_defs(fields_data)
        sd.fields = head
        sd.field_count = count
      end

      if !expr.resolved_type then expr.resolved_type = Type.new(TK_CLASS)
      else expr.resolved_type.kind = TK_CLASS end
      expr.resolved_type.name = buf
      expr.is_fresh_alloc = true
    end

    def analyze_array_literal(expr)
      elem_type = nil
      expr.elems.each do |e|
        analyze_expr(e)
        get_expr_type(e)
        if !elem_type
          elem_type = e.resolved_type.clone
        elsif elem_type.kind != TK_UNKNOWN &&
              e.resolved_type.kind != TK_UNKNOWN &&
              elem_type != e.resolved_type
          sem_error(expr.line, "array elements must all have the same type")
        end
      end
      if !expr.resolved_type then expr.resolved_type = Type.new(TK_ARRAY)
      else expr.resolved_type.kind = TK_ARRAY end
      expr.resolved_type.elem = elem_type || Type.new(TK_UNKNOWN)
      expr.is_fresh_alloc = true
    end

    def analyze_hash_literal(expr)
      key_type = nil
      val_type = nil
      expr.pairs.each do |pair|
        analyze_expr(pair.key)
        analyze_expr(pair.value)
        get_expr_type(pair.key)
        get_expr_type(pair.value)
        if !key_type
          key_type = pair.key.resolved_type.clone
        elsif key_type.kind != TK_UNKNOWN &&
              pair.key.resolved_type.kind != TK_UNKNOWN &&
              key_type != pair.key.resolved_type
          sem_error(expr.line, "hash keys must all have the same type")
        end
        if !val_type
          val_type = pair.value.resolved_type.clone
        elsif val_type.kind != TK_UNKNOWN &&
              pair.value.resolved_type.kind != TK_UNKNOWN &&
              val_type != pair.value.resolved_type
          sem_error(expr.line, "hash values must all have the same type")
        end
      end
      if !expr.resolved_type then expr.resolved_type = Type.new(TK_HASH)
      else expr.resolved_type.kind = TK_HASH end
      expr.resolved_type.key = key_type || Type.new(TK_UNKNOWN)
      expr.resolved_type.elem = val_type || Type.new(TK_UNKNOWN)
      expr.is_fresh_alloc = true
    end

    def analyze_typed_empty_array(expr)
      if !expr.resolved_type then expr.resolved_type = Type.new(TK_ARRAY)
      else expr.resolved_type.kind = TK_ARRAY end
      if expr.elem_name
        ename = expr.elem_name
        sd = lookup_struct(ename)
        unless sd
          sem_error(expr.line, "undefined type '#{ename}'")
        end
        ek = (sd&.is_class) ? TK_CLASS : TK_STRUCT
        expr.resolved_type.elem = Type.new(ek)
        expr.resolved_type.elem.name = ename
      else
        expr.resolved_type.elem = Type.new(expr.elem_type)
      end
      expr.is_fresh_alloc = true
    end

    def analyze_typed_empty_hash(expr)
      if !expr.resolved_type then expr.resolved_type = Type.new(TK_HASH)
      else expr.resolved_type.kind = TK_HASH end
      expr.resolved_type.key = Type.new(expr.key_type)
      if expr.value_name
        vname = expr.value_name
        sd = lookup_struct(vname)
        unless sd
          sem_error(expr.line, "undefined type '#{vname}'")
        end
        vk = (sd&.is_class) ? TK_CLASS : TK_STRUCT
        expr.resolved_type.elem = Type.new(vk)
        expr.resolved_type.elem.name = vname
      else
        expr.resolved_type.elem = Type.new(expr.value_type)
      end
      expr.is_fresh_alloc = true
    end

    def analyze_optional_check(expr)
      analyze_expr(expr.operand)
      operand = expr.operand
      ot = get_expr_type(operand).kind

      is_opt = operand.resolved_type&.is_optional
      if !is_opt && operand.is_a?(AST::Ident)
        sym = lookup(operand.name)
        is_opt = sym.type.is_optional if sym
      end

      if !is_opt && ot != TK_STRING && ot != TK_CLASS
        sem_error(expr.line, "cannot use '?' on non-optional type")
      end

      if !expr.resolved_type then expr.resolved_type = Type.new(TK_BOOL)
      else expr.resolved_type.kind = TK_BOOL end
    end

    def analyze_block(block)
      return unless block.is_a?(AST::Block)
      push_scope
      analyze_stmts(block.stmts)
      pop_scope
    end

    def analyze_stmt(node)
      return unless node

      case node
      when AST::Decl
        analyze_expr(node.value)
        check_not_void(node.line, node.value, 'as initializer')
        type = get_expr_type(node.value)
        add_symbol(node.line, node.name, type, node.is_const)

      when AST::If
        analyze_if(node)

      when AST::While
        analyze_while(node)

      when AST::For
        analyze_for(node)

      when AST::Break
        if @in_loop == 0
          sem_error(node.line, "'break' outside of loop")
        end
        if node.value
          analyze_expr(node.value)
          bt = get_expr_type(node.value)
          if bt.kind != TK_UNKNOWN && bt.kind != TK_VOID
            if !@loop_result_set
              @loop_result_type = bt.clone
              @loop_result_set = true
            elsif @loop_result_type != bt
              sem_error(node.line, "break/continue value type does not match previous")
            end
          end
        end

      when AST::Continue
        if @in_loop == 0
          sem_error(node.line, "'continue' outside of loop")
        end
        if node.value
          analyze_expr(node.value)
          ct = get_expr_type(node.value)
          if ct.kind != TK_UNKNOWN && ct.kind != TK_VOID
            if !@loop_result_set
              @loop_result_type = ct.clone
              @loop_result_set = true
            elsif @loop_result_type != ct
              sem_error(node.line, "break/continue value type does not match previous")
            end
          end
        end

      when AST::TypeDef
        analyze_type_def(node)

      when AST::FuncDef
        analyze_func_def(node)

      when AST::ExternBlock
        node.decls.each { |d| analyze_stmt(d) }

      when AST::ExternFunc
        analyze_extern_func(node)

      when AST::ExternVar
        vtype = node.type_info.to_type
        add_extern_var(node.line, node.name, vtype, false)

      when AST::ExternLet
        ltype = node.type_info.to_type
        add_extern_var(node.line, node.name, ltype, true)

      when AST::Return
        if !@in_function
          sem_error(node.line, "'return' outside of function")
        elsif node.value
          analyze_expr(node.value)
          ret_type = get_expr_type(node.value)
          if !@current_func_return_type
            if ret_type.kind != TK_UNKNOWN && ret_type.kind != TK_VOID
              @current_func_return_type = ret_type.clone
            end
          end
        end

      when AST::Block
        analyze_block(node)

      else
        # Expression statement
        analyze_expr(node)
      end
    end

    def analyze_if(node)
      analyze_expr(node.cond)
      check_not_void(node.line, node.cond, 'as condition')

      # Type narrowing: if condition is x? where x is optional
      cond = node.cond
      narrowing = false
      if cond.is_a?(AST::OptionalCheck) && cond.operand.is_a?(AST::Ident)
        narrow_name = cond.operand.name
        orig = lookup(narrow_name)
        if orig&.type&.is_optional
          narrowing = true
          if node.then_b.is_a?(AST::Block)
            push_scope
            narrowed = orig.type.clone
            narrowed.is_optional = false
            add_symbol(node.line, narrow_name, narrowed, orig.is_const)
            analyze_stmts(node.then_b.stmts)
            pop_scope
          end
        end
      end

      analyze_block(node.then_b) unless narrowing

      if node.else_b
        if node.else_b.is_a?(AST::If)
          analyze_stmt(node.else_b)
        else
          analyze_block(node.else_b)
        end
        # Compute expression type from matching branch types
        then_b = node.then_b
        else_b = node.else_b
        then_t = TK_UNKNOWN
        else_t = TK_UNKNOWN
        if then_b.is_a?(AST::Block) && !then_b.stmts.empty?
          then_t = get_expr_type(then_b.stmts.last).kind
        end
        if else_b.is_a?(AST::If)
          else_t = else_b.resolved_type&.kind || TK_UNKNOWN
        elsif else_b.is_a?(AST::Block) && !else_b.stmts.empty?
          else_t = get_expr_type(else_b.stmts.last).kind
        end
        if then_t != TK_UNKNOWN && then_t != TK_VOID && then_t == else_t
          if !node.resolved_type then node.resolved_type = Type.new(then_t)
          else node.resolved_type.kind = then_t end
          node.is_fresh_alloc = true if ref_type?(then_t)
          # Propagate struct/class name
          if (then_t == TK_STRUCT || then_t == TK_CLASS) &&
             then_b.is_a?(AST::Block) && !then_b.stmts.empty?
            last_node = then_b.stmts.last
            if last_node&.resolved_type&.name
              node.resolved_type.name = last_node.resolved_type.name.dup
            end
          end
        end
      else
        # If without else -> optional type
        then_b = node.then_b
        if then_b.is_a?(AST::Block) && !then_b.stmts.empty?
          then_t = get_expr_type(then_b.stmts.last).kind
          if then_t != TK_UNKNOWN && then_t != TK_VOID
            if !node.resolved_type then node.resolved_type = Type.new(then_t)
            else node.resolved_type.kind = then_t end
            node.resolved_type.is_optional = true
            node.is_fresh_alloc = true if ref_type?(then_t)
          end
        end
      end
    end

    def analyze_while(node)
      analyze_expr(node.cond)
      check_not_void(node.line, node.cond, 'as condition')
      saved_lrt = @loop_result_type
      saved_lrs = @loop_result_set
      @loop_result_type = nil
      @loop_result_set = false
      @in_loop += 1
      analyze_block(node.body)
      @in_loop -= 1
      if @loop_result_set && @loop_result_type
        node.resolved_type = @loop_result_type.clone
        unless is_always_true(node.cond)
          node.resolved_type.is_optional = true
        end
        node.is_fresh_alloc = true if ref_type?(node.resolved_type.kind)
      end
      @loop_result_type = saved_lrt
      @loop_result_set = saved_lrs
    end

    def analyze_for(node)
      push_scope
      analyze_stmt(node.init) if node.init
      analyze_expr(node.cond)
      check_not_void(node.line, node.cond, 'as condition')
      analyze_expr(node.update) if node.update
      saved_lrt = @loop_result_type
      saved_lrs = @loop_result_set
      @loop_result_type = nil
      @loop_result_set = false
      @in_loop += 1
      if node.body.is_a?(AST::Block)
        analyze_stmts(node.body.stmts)
      end
      @in_loop -= 1
      if @loop_result_set && @loop_result_type
        node.resolved_type = @loop_result_type.clone
        node.resolved_type.is_optional = true  # for loops are always conditional
        node.is_fresh_alloc = true if ref_type?(node.resolved_type.kind)
      end
      @loop_result_type = saved_lrt
      @loop_result_set = saved_lrs
      pop_scope
    end

    def analyze_type_def(node)
      is_class = node.is_class
      def_name = node.name
      if lookup_struct(def_name)
        sem_error(node.line, "#{is_class ? 'class' : 'struct'} '#{def_name}' already defined")
        return
      end
      sd = register_struct(def_name, is_class)

      fields_head = nil
      fields_tail = nil
      field_count = 0

      node.fields.each do |field|
        # Check for duplicate fields
        existing = fields_head
        while existing
          if existing.name == field.name
            sem_error(field.line, "duplicate field '#{field.name}' in #{is_class ? 'class' : 'struct'} '#{def_name}'")
          end
          existing = existing.next
        end

        # Weak not allowed in structs
        if !is_class && field.is_weak
          sem_error(field.line, "'weak' is only allowed in class definitions")
        end

        fd = StructFieldDef.new
        fd.name = field.name
        fd.is_const = field.is_const
        fd.is_weak = field.is_weak if is_class

        if field.type_info
          resolve_type_info(field.type_info)
          fti = field.type_info
          sn = fti.name
          if sn
            fsd = lookup_struct(sn)
            unless fsd
              sem_error(field.line, "undefined type '#{sn}'")
            end
          end
          fd.type = fti.to_type
          # Resolve struct that is actually a class
          if sn
            fsd = lookup_struct(sn)
            fd.type.kind = TK_CLASS if fsd&.is_class
          end
          fd.has_default = false
        elsif field.default_value
          analyze_expr(field.default_value)
          tk = get_expr_type(field.default_value).kind
          fd.type = Type.new(tk)
          fd.has_default = true
          fd.default_value = field.default_value
        end

        # Validate weak: only on class-typed fields
        if fd.is_weak
          is_class_field = fd.type&.kind == TK_CLASS
          unless is_class_field
            sem_error(field.line, "'weak' can only be used on class-typed fields")
          end
        end

        if fields_tail
          fields_tail.next = fd
        else
          fields_head = fd
        end
        fields_tail = fd
        field_count += 1
      end

      sd.fields = fields_head
      sd.field_count = field_count
    end

    def analyze_func_def(node)
      # Resolve object types in parameters
      node.params.each do |p|
        resolve_type_info(p.type_info)
      end

      # Collect parameter types
      param_types = node.params.map do |p|
        pt = p.type_info.to_type
        # Resolve struct that is actually a class
        if pt.kind == TK_STRUCT && p.type_info.name
          psd = lookup_struct(p.type_info.name)
          pt.kind = TK_CLASS if psd&.is_class
        end
        pt
      end

      # Add function to current scope (before body for recursion)
      void_type = Type.new(TK_VOID)
      func_sym = add_function(node.line, node.name, void_type,
                               param_types.size, param_types, false)

      # Analyze function body in new scope
      push_scope

      # Add parameters to function scope (const by default)
      node.params.each do |p|
        ptype = p.type_info.to_type
        if ptype.kind == TK_STRUCT && p.type_info.name
          psd = lookup_struct(p.type_info.name)
          ptype.kind = TK_CLASS if psd&.is_class
        end
        add_symbol(p.line, p.name, ptype, true)
      end

      old_in_function = @in_function
      old_return_type = @current_func_return_type
      @in_function = true
      @current_func_return_type = nil

      if node.body.is_a?(AST::Block)
        analyze_stmts(node.body.stmts)
        # Infer return type from last expression
        if !@current_func_return_type && !node.body.stmts.empty?
          last = node.body.stmts.last
          lt = get_expr_type(last)
          if lt.kind != TK_UNKNOWN && lt.kind != TK_VOID
            @current_func_return_type = lt.clone
          end
        end
      end

      # Update function return type
      if func_sym && @current_func_return_type
        func_sym.type = @current_func_return_type.clone
      end

      @in_function = old_in_function
      @current_func_return_type = old_return_type
      pop_scope
    end

    def analyze_extern_func(node)
      param_types = node.params.map { |p| p.type_info.to_type }
      ret_type = node.return_type ? node.return_type.to_type : Type.new(TK_VOID)
      add_function(node.line, node.name, ret_type, param_types.size, param_types, true)
    end

    def analyze_stmts(stmts)
      stmts.each { |s| analyze_stmt(s) }
    end
  end
end
