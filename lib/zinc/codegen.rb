# frozen_string_literal: true

module Zinc
  # ARC scope variable tracking
  CGScopeVar = Struct.new(:name, :type_name, :is_value_type) do
    def initialize(name = nil, type_name = nil, is_value_type = false)
      super(name, type_name, is_value_type)
    end
  end

  # Codegen scope for ARC release tracking
  CGScope = Struct.new(:ref_vars, :is_loop, :parent) do
    def initialize(ref_vars = [], is_loop = false, parent = nil)
      super(ref_vars, is_loop, parent)
    end
  end

  class Codegen
    attr_accessor :indent_level, :temp_counter, :string_counter,
                  :loop_expr_temp, :loop_expr_optional, :loop_expr_type

    def initialize(c_file, h_file, semantic_ctx, output_base, source_file)
      @c_file = c_file
      @h_file = h_file
      @sem = semantic_ctx
      @output_base = output_base
      @source_file = source_file

      @indent_level = 0
      @temp_counter = 0
      @string_counter = 0
      @loop_expr_temp = -1
      @loop_expr_optional = false
      @loop_expr_type = TK_VOID

      @scope = nil       # top of CGScope stack (linked via parent)
      @narrowed = []     # stack of narrowed optional variable names
    end

    # ------------------------------------------------------------------
    # Emit helpers
    # ------------------------------------------------------------------

    def emit(s)
      @c_file.write(s)
    end

    def emitf(fmt, *args)
      @c_file.write(sprintf(fmt, *args))
    end

    def emit_indent
      @indent_level.times { @c_file.write("    ") }
    end

    def emit_header(s)
      @h_file.write(s)
    end

    def emit_line(line)
      if line > 0 && @source_file
        @c_file.write("#line #{line} \"#{@source_file}\"\n")
      end
    end

    # ------------------------------------------------------------------
    # Type helpers
    # ------------------------------------------------------------------

    TYPE_TO_C = {
      TK_INT    => "int64_t",
      TK_FLOAT  => "double",
      TK_STRING => "ZnString*",
      TK_BOOL   => "bool",
      TK_CHAR   => "char",
      TK_VOID   => "void",
      TK_STRUCT => "/* struct */",
      TK_CLASS  => "/* class */",
      TK_ARRAY  => "ZnArray*",
      TK_HASH   => "ZnHash*",
    }.freeze

    OPT_TYPE_FOR = {
      TK_INT   => "ZnOpt_int",
      TK_FLOAT => "ZnOpt_float",
      TK_BOOL  => "ZnOpt_bool",
      TK_CHAR  => "ZnOpt_char",
    }.freeze

    def type_to_c(kind)
      TYPE_TO_C[kind] || "int64_t"
    end

    def opt_type_for(kind)
      OPT_TYPE_FOR[kind]
    end

    def ref_type?(kind)
      kind == TK_STRING || kind == TK_CLASS || kind == TK_ARRAY || kind == TK_HASH
    end

    def expr_is_string(expr)
      expr && expr.resolved_type && expr.resolved_type.kind == TK_STRING
    end

    # ------------------------------------------------------------------
    # ARC scope management
    # ------------------------------------------------------------------

    def push_scope(is_loop = false)
      s = CGScope.new([], is_loop, @scope)
      @scope = s
    end

    def pop_scope
      @scope = @scope.parent if @scope
    end

    def scope_add_ref(name, type_name)
      return unless @scope
      @scope.ref_vars.unshift(CGScopeVar.new(name, type_name, false))
    end

    def scope_add_value_type(name, struct_name)
      return unless @scope
      @scope.ref_vars.unshift(CGScopeVar.new(name, struct_name, true))
    end

    def emit_value_type_field_releases(prefix, sd)
      f = sd.fields
      while f
        ft = f.type
        if ft
          case ft.kind
          when TK_STRING
            emit_indent
            emitf("__zn_str_release(%s.%s);\n", prefix, f.name)
          when TK_ARRAY
            emit_indent
            emitf("__zn_arr_release(%s.%s);\n", prefix, f.name)
          when TK_HASH
            emit_indent
            emitf("__zn_hash_release(%s.%s);\n", prefix, f.name)
          when TK_CLASS
            if ft.name
              emit_indent
              emitf("__%s_release(%s.%s);\n", ft.name, prefix, f.name)
            end
          when TK_STRUCT
            if ft.name
              inner = @sem.lookup_struct(ft.name)
              if inner
                nested = "#{prefix}.#{f.name}"
                emit_value_type_field_releases(nested, inner)
              end
            end
          end
        end
        f = f.next
      end
    end

    def emit_var_release(v)
      if v.is_value_type
        sd = @sem.lookup_struct(v.type_name)
        emit_value_type_field_releases(v.name, sd) if sd
      else
        emit_indent
        emitf("__%s_release(%s);\n", v.type_name, v.name)
      end
    end

    def emit_scope_releases
      return unless @scope
      @scope.ref_vars.each { |v| emit_var_release(v) }
    end

    def emit_all_scope_releases
      s = @scope
      while s
        s.ref_vars.each { |v| emit_var_release(v) }
        s = s.parent
      end
    end

    def find_loop_scope
      s = @scope
      while s
        return s if s.is_loop
        s = s.parent
      end
      nil
    end

    # ------------------------------------------------------------------
    # ARC retain/release type dispatch helpers
    # ------------------------------------------------------------------

    def emit_retain_call(expr, type)
      return unless type
      case type.kind
      when TK_STRING then emitf("__zn_str_retain(%s)", expr)
      when TK_CLASS  then emitf("__%s_retain(%s)", type.name, expr) if type.name
      when TK_ARRAY  then emitf("__zn_arr_retain(%s)", expr)
      when TK_HASH   then emitf("__zn_hash_retain(%s)", expr)
      end
    end

    def emit_release_call(expr, type)
      return unless type
      case type.kind
      when TK_STRING then emitf("__zn_str_release(%s)", expr)
      when TK_CLASS  then emitf("__%s_release(%s)", type.name, expr) if type.name
      when TK_ARRAY  then emitf("__zn_arr_release(%s)", expr)
      when TK_HASH   then emitf("__zn_hash_release(%s)", expr)
      end
    end

    def emit_retain_open(type)
      return unless type
      case type.kind
      when TK_STRING then emit("__zn_str_retain(")
      when TK_CLASS  then emitf("__%s_retain(", type.name) if type.name
      when TK_ARRAY  then emit("__zn_arr_retain(")
      when TK_HASH   then emit("__zn_hash_retain(")
      end
    end

    def emit_release_open(type)
      return unless type
      case type.kind
      when TK_STRING then emit("__zn_str_release(")
      when TK_CLASS  then emitf("__%s_release(", type.name) if type.name
      when TK_ARRAY  then emit("__zn_arr_release(")
      when TK_HASH   then emit("__zn_hash_release(")
      end
    end

    def emit_box_call(expr, type)
      return unless type
      case type.kind
      when TK_STRING then emitf("__zn_val_string(%s)", expr)
      when TK_ARRAY  then emitf("__zn_val_array((ZnArray*)(%s))", expr)
      when TK_HASH   then emitf("__zn_val_hash((ZnHash*)(%s))", expr)
      when TK_CLASS  then emitf("__zn_val_ref(%s)", expr)
      end
    end

    # ------------------------------------------------------------------
    # Narrowing stack helpers
    # ------------------------------------------------------------------

    def push_narrowed(name)
      @narrowed.push(name)
    end

    def pop_narrowed
      @narrowed.pop
    end

    def narrowed?(name)
      @narrowed.include?(name)
    end

    # ------------------------------------------------------------------
    # String literal collection
    # ------------------------------------------------------------------

    # Walk the AST, yielding each node to the block
    def ast_walk(node, &block)
      return unless node
      yield node

      case node
      when AST::Program
        node.stmts.each { |s| ast_walk(s, &block) }
      when AST::Block
        node.stmts.each { |s| ast_walk(s, &block) }
      when AST::BinOp
        ast_walk(node.left, &block)
        ast_walk(node.right, &block)
      when AST::UnaryOp
        ast_walk(node.operand, &block)
      when AST::Assign
        ast_walk(node.target, &block)
        ast_walk(node.value, &block)
      when AST::CompoundAssign
        ast_walk(node.target, &block)
        ast_walk(node.value, &block)
      when AST::IncDec
        ast_walk(node.target, &block)
      when AST::Decl
        ast_walk(node.value, &block)
      when AST::If
        ast_walk(node.cond, &block)
        ast_walk(node.then_b, &block)
        ast_walk(node.else_b, &block)
      when AST::While
        ast_walk(node.cond, &block)
        ast_walk(node.body, &block)
      when AST::For
        ast_walk(node.init, &block)
        ast_walk(node.cond, &block)
        ast_walk(node.update, &block)
        ast_walk(node.body, &block)
      when AST::FuncDef
        ast_walk(node.body, &block)
      when AST::Call
        node.args.each { |a| ast_walk(a, &block) }
      when AST::Return
        ast_walk(node.value, &block)
      when AST::Break
        ast_walk(node.value, &block)
      when AST::Continue
        ast_walk(node.value, &block)
      when AST::FieldAccess
        ast_walk(node.object, &block)
      when AST::TypeDef
        node.fields.each { |f| ast_walk(f, &block) }
      when AST::StructField
        ast_walk(node.default_value, &block)
      when AST::NamedArg
        ast_walk(node.value, &block)
      when AST::Tuple
        node.elements.each { |e| ast_walk(e, &block) }
      when AST::ObjectLiteral
        node.fields.each { |f| ast_walk(f, &block) }
      when AST::Index
        ast_walk(node.object, &block)
        ast_walk(node.index, &block)
      when AST::ArrayLiteral
        node.elems.each { |e| ast_walk(e, &block) }
      when AST::HashLiteral
        node.pairs.each { |p| ast_walk(p, &block) }
      when AST::HashPair
        ast_walk(node.key, &block)
        ast_walk(node.value, &block)
      when AST::OptionalCheck
        ast_walk(node.operand, &block)
      when AST::ExternBlock
        node.decls.each { |d| ast_walk(d, &block) }
      end
    end

    # Escape a string value for C string literal output
    def emit_c_string_escaped(s)
      s.each_char do |ch|
        case ch
        when "\n" then @c_file.write("\\n")
        when "\t" then @c_file.write("\\t")
        when "\r" then @c_file.write("\\r")
        when "\\" then @c_file.write("\\\\")
        when '"'  then @c_file.write('\\"')
        else           @c_file.write(ch)
        end
      end
    end

    # Walk the AST collecting string literals: assign each a unique ID
    # and emit a static struct definition for it.
    def collect_string_literals(root)
      ast_walk(root) do |node|
        next unless node.is_a?(AST::StringLit)

        id = @string_counter
        @string_counter += 1
        node.string_id = id

        len = node.value.length
        emitf(
          "static struct { int32_t _rc; int32_t _len; char _data[%d]; } __zn_str_%d = {-1, %d, \"",
          len + 1, id, len
        )
        emit_c_string_escaped(node.value)
        emit("\"};\n")
      end
    end

    # ------------------------------------------------------------------
    # Top-level code generation
    # ------------------------------------------------------------------

    def generate(root)
      return unless root.is_a?(AST::Program)

      base = basename_of(@output_base)
      guard = make_header_guard(base)

      # Header preamble
      emit_header("#ifndef #{guard}\n")
      emit_header("#define #{guard}\n\n")
      emit_header("#include <stdint.h>\n")
      emit_header("#include <stdbool.h>\n\n")
      emit_header("#include \"zinc_runtime.h\"\n\n")

      # C file includes
      emit("#include <stdio.h>\n")
      emit("#include <stdlib.h>\n")
      emit("#include <string.h>\n")
      emit("#include <stdint.h>\n")
      emit("#include <inttypes.h>\n")
      emit("#include <stdbool.h>\n")
      emitf("#include \"%s.h\"\n\n", base)

      # Generate struct typedefs (non-class) to header
      root.stmts.each do |s|
        if s.is_a?(AST::TypeDef) && !s.is_class
          gen_struct_def(s)
        end
      end

      # Generate class typedefs and ARC functions
      root.stmts.each do |s|
        if s.is_a?(AST::TypeDef) && s.is_class
          gen_class_def(s)
        end
      end

      # Generate tuple typedefs
      gen_tuple_typedefs

      # Generate anonymous object typedefs + ARC functions
      gen_object_typedefs

      # Generate collection helper functions (retain/release wrappers, hashcode, equals)
      gen_collection_helpers

      # Collect string literals and emit static structs
      collect_string_literals(root)
      emit("\n")

      # Generate extern declarations (to header)
      root.stmts.each do |s|
        if s.is_a?(AST::ExternBlock)
          gen_extern_block(s)
        end
      end

      # Generate all functions
      root.stmts.each do |s|
        if s.is_a?(AST::FuncDef)
          gen_func_def(s)
        end
      end

      # Close header guard
      emit_header("\n#endif\n")
    end

    private

    # Extract basename from a path (everything after the last '/')
    def basename_of(path)
      idx = path.rindex("/")
      idx ? path[(idx + 1)..] : path
    end

    # Build a C header guard from a base filename
    def make_header_guard(base)
      guard = "#{base}_H"
      guard.gsub!(/[a-z]/) { |c| c.upcase }
      guard.gsub!(/[-.]/, "_")
      guard
    end
  end
end
