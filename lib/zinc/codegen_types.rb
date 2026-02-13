# frozen_string_literal: true

module Zinc
  class Codegen
    # Generate struct typedef to header
    def gen_struct_def(node)
      name = node.name
      sd = @sem.lookup_struct(name)
      return unless sd

      emit_header("typedef struct {\n")
      fd = sd.fields
      while fd
        if fd.type.kind == TK_STRUCT && fd.type.name
          emit_header("    #{fd.type.name} #{fd.name};\n")
        else
          emit_header("    #{type_to_c(fd.type.kind)} #{fd.name};\n")
        end
        fd = fd.next
      end
      emit_header("} #{name};\n\n")
    end

    # Generate class typedef (to header) and ARC alloc/retain/release (to C file)
    def gen_class_def(node)
      name = node.name
      sd = @sem.lookup_struct(name)
      return unless sd

      # Typedef to header (named struct tag for self-referential types)
      emit_header("typedef struct #{name} {\n")
      emit_header("    int _rc;\n")
      fd = sd.fields
      while fd
        case fd.type.kind
        when TK_STRING
          emit_header("    ZnString *#{fd.name};\n")
        when TK_CLASS
          if fd.type.name
            emit_header("    struct #{fd.type.name} *#{fd.name};\n")
          else
            emit_header("    #{type_to_c(fd.type.kind)} #{fd.name};\n")
          end
        when TK_STRUCT
          if fd.type.name
            emit_header("    #{fd.type.name} #{fd.name};\n")
          else
            emit_header("    #{type_to_c(fd.type.kind)} #{fd.name};\n")
          end
        else
          emit_header("    #{type_to_c(fd.type.kind)} #{fd.name};\n")
        end
        fd = fd.next
      end
      emit_header("} #{name};\n\n")

      # Alloc function
      emit("static #{name}* __#{name}_alloc(void) {\n")
      emit("    #{name} *self = calloc(1, sizeof(#{name}));\n")
      emit("    self->_rc = 1;\n")
      emit("    return self;\n")
      emit("}\n\n")

      # Retain function
      emit("static void __#{name}_retain(#{name} *self) {\n")
      emit("    if (self) self->_rc++;\n")
      emit("}\n\n")

      # Release function
      emit("static void __#{name}_release(#{name} *self) {\n")
      emit("    if (self && --(self->_rc) == 0) {\n")
      emit_nested_releases('self->', sd)
      emit("        free(self);\n")
      emit("    }\n")
      emit("}\n\n")
    end

    # Generate tuple typedefs (anonymous struct types with __ZnTuple prefix)
    def gen_tuple_typedefs
      @sem.struct_defs.each_value do |sd|
        next unless sd.name.start_with?('__ZnTuple')
        emit_header("typedef struct {\n")
        fd = sd.fields
        while fd
          if fd.type.kind == TK_STRUCT && fd.type.name
            emit_header("    #{fd.type.name} #{fd.name};\n")
          else
            emit_header("    #{type_to_c(fd.type.kind)} #{fd.name};\n")
          end
          fd = fd.next
        end
        emit_header("} #{sd.name};\n\n")
      end
    end

    # Generate anonymous object typedefs + ARC functions (__obj prefix)
    def gen_object_typedefs
      @sem.struct_defs.each_value do |sd|
        next unless sd.name.start_with?('__obj')
        name = sd.name

        # Typedef to header
        emit_header("typedef struct #{name} {\n")
        emit_header("    int _rc;\n")
        fd = sd.fields
        while fd
          case fd.type.kind
          when TK_STRING
            emit_header("    ZnString *#{fd.name};\n")
          when TK_CLASS
            if fd.type.name
              emit_header("    struct #{fd.type.name} *#{fd.name};\n")
            else
              emit_header("    #{type_to_c(fd.type.kind)} #{fd.name};\n")
            end
          when TK_STRUCT
            if fd.type.name
              emit_header("    #{fd.type.name} #{fd.name};\n")
            else
              emit_header("    #{type_to_c(fd.type.kind)} #{fd.name};\n")
            end
          else
            emit_header("    #{type_to_c(fd.type.kind)} #{fd.name};\n")
          end
          fd = fd.next
        end
        emit_header("} #{name};\n\n")

        # Alloc
        emit("static #{name}* __#{name}_alloc(void) {\n")
        emit("    #{name} *self = calloc(1, sizeof(#{name}));\n")
        emit("    self->_rc = 1;\n")
        emit("    return self;\n")
        emit("}\n\n")

        # Retain
        emit("static void __#{name}_retain(#{name} *self) {\n")
        emit("    if (self) self->_rc++;\n")
        emit("}\n\n")

        # Release
        emit("static void __#{name}_release(#{name} *self) {\n")
        emit("    if (self && --(self->_rc) == 0) {\n")
        emit_nested_releases('self->', sd)
        emit("        free(self);\n")
        emit("    }\n")
        emit("}\n\n")
      end
    end

    # Emit release calls for ref-counted fields in a struct/class
    def emit_nested_releases(prefix, sd)
      fd = sd.fields
      while fd
        unless fd.is_weak
          ft = fd.type
          if ft
            case ft.kind
            when TK_STRING
              emit("        __zn_str_release(#{prefix}#{fd.name});\n")
            when TK_CLASS
              emit("        __#{ft.name}_release(#{prefix}#{fd.name});\n") if ft.name
            when TK_ARRAY
              emit("        __zn_arr_release(#{prefix}#{fd.name});\n")
            when TK_HASH
              emit("        __zn_hash_release(#{prefix}#{fd.name});\n")
            when TK_STRUCT
              if ft.name
                inner = @sem.lookup_struct(ft.name)
                if inner
                  emit_nested_releases("#{prefix}#{fd.name}.", inner)
                end
              end
            end
          end
        end
        fd = fd.next
      end
    end

    # Generate extern declaration to header
    def gen_extern_decl(decl)
      case decl
      when AST::ExternFunc
        ret_ti = decl.return_type
        @h_file.write('extern ')
        if !ret_ti
          @h_file.write('void')
        elsif ret_ti.kind == TK_STRING
          @h_file.write('const char*')
        elsif ret_ti.kind == TK_STRUCT && ret_ti.name
          sd = @sem.lookup_struct(ret_ti.name)
          if sd&.is_class
            @h_file.write("#{ret_ti.name} *")
          else
            @h_file.write(ret_ti.name)
          end
        else
          @h_file.write(type_to_c(ret_ti.kind))
        end
        @h_file.write(" #{decl.name}(")

        first = true
        decl.params.each do |p|
          @h_file.write(', ') unless first
          ti = p.type_info
          if ti.kind == TK_STRING
            @h_file.write("const char* #{p.name}")
          elsif ti.kind == TK_STRUCT && ti.name
            sd = @sem.lookup_struct(ti.name)
            if sd&.is_class
              @h_file.write("#{ti.name} *#{p.name}")
            else
              @h_file.write("#{ti.name} #{p.name}")
            end
          else
            @h_file.write("#{type_to_c(ti.kind)} #{p.name}")
          end
          first = false
        end
        @h_file.write('void') if first
        @h_file.write(");\n")

      when AST::ExternVar
        ti = decl.type_info
        if ti.kind == TK_STRING
          @h_file.write("extern const char* #{decl.name};\n")
        elsif ti.kind == TK_STRUCT && ti.name
          sd = @sem.lookup_struct(ti.name)
          if sd&.is_class
            @h_file.write("extern #{ti.name} *#{decl.name};\n")
          else
            @h_file.write("extern #{ti.name} #{decl.name};\n")
          end
        else
          @h_file.write("extern #{type_to_c(ti.kind)} #{decl.name};\n")
        end

      when AST::ExternLet
        ti = decl.type_info
        if ti.kind == TK_STRING
          @h_file.write("extern const char* const #{decl.name};\n")
        elsif ti.kind == TK_STRUCT && ti.name
          sd = @sem.lookup_struct(ti.name)
          if sd&.is_class
            @h_file.write("extern #{ti.name} *const #{decl.name};\n")
          else
            @h_file.write("extern const #{ti.name} #{decl.name};\n")
          end
        else
          @h_file.write("extern const #{type_to_c(ti.kind)} #{decl.name};\n")
        end
      end
    end

    # Generate extern block
    def gen_extern_block(block)
      block.decls.each { |d| gen_extern_decl(d) }
    end

    # Generate collection helper functions for all struct-like types
    def gen_collection_helpers
      # Pass 1: Forward declarations
      @sem.struct_defs.each_value do |sd|
        name = sd.name
        if sd.is_class
          emit("static void __zn_ret_#{name}(void *p);\n")
          emit("static void __zn_rel_#{name}(void *p);\n")
        else
          emit("static void __zn_val_rel_#{name}(void *p);\n")
        end
        emit("static unsigned int __zn_hash_#{name}(ZnValue v);\n")
        emit("static bool __zn_eq_#{name}(ZnValue a, ZnValue b);\n")
      end
      emit("\n")

      # Pass 2: Implementations
      @sem.struct_defs.each_value do |sd|
        name = sd.name
        fields = sd.fields

        # Retain/release wrappers for reference types
        if sd.is_class
          emit("static void __zn_ret_#{name}(void *p) { __#{name}_retain((#{name}*)p); }\n")
          emit("static void __zn_rel_#{name}(void *p) { __#{name}_release((#{name}*)p); }\n")
        end

        # Value-type release
        unless sd.is_class
          emit("static void __zn_val_rel_#{name}(void *p) {\n")
          emit("    #{name} *self = (#{name}*)p;\n")
          fd = fields
          while fd
            ft = fd.type
            if ft
              case ft.kind
              when TK_STRING
                emit("    __zn_str_release(self->#{fd.name});\n")
              when TK_ARRAY
                emit("    __zn_arr_release(self->#{fd.name});\n")
              when TK_HASH
                emit("    __zn_hash_release(self->#{fd.name});\n")
              when TK_CLASS
                emit("    __#{ft.name}_release(self->#{fd.name});\n") if ft.name
              when TK_STRUCT
                if ft.name
                  inner = @sem.lookup_struct(ft.name)
                  if inner
                    emit_nested_releases("self->#{fd.name}.", inner)
                  end
                end
              end
            end
            fd = fd.next
          end
          emit("    free(self);\n")
          emit("}\n")
        end

        # Hashcode — field-by-field djb2
        emit("static unsigned int __zn_hash_#{name}(ZnValue v) {\n")
        emit("    #{name} *self = (#{name}*)v.as.ptr;\n")
        emit("    unsigned int h = 5381;\n")
        fd = fields
        while fd
          ft = fd.type
          if ft
            fname = fd.name
            case ft.kind
            when TK_INT
              emit("    h = ((h << 5) + h) ^ (unsigned int)((uint64_t)self->#{fname} ^ ((uint64_t)self->#{fname} >> 32));\n")
            when TK_FLOAT
              emit("    { union { double d; uint64_t u; } __cv; __cv.d = self->#{fname}; h = ((h << 5) + h) ^ (unsigned int)(__cv.u ^ (__cv.u >> 32)); }\n")
            when TK_BOOL
              emit("    h = ((h << 5) + h) ^ (self->#{fname} ? 1u : 0u);\n")
            when TK_CHAR
              emit("    h = ((h << 5) + h) ^ (unsigned int)self->#{fname};\n")
            when TK_STRING
              emit("    { ZnValue __sv = __zn_val_string(self->#{fname}); h = ((h << 5) + h) ^ __zn_val_hashcode(__sv); }\n")
            when TK_CLASS
              emit("    h = ((h << 5) + h) ^ (unsigned int)((uintptr_t)self->#{fname});\n") if ft.name
            when TK_STRUCT
              if ft.name
                emit("    { ZnValue __sv; __sv.tag = ZN_TAG_VAL; __sv.as.ptr = &self->#{fname}; h = ((h << 5) + h) ^ __zn_hash_#{ft.name}(__sv); }\n")
              end
            when TK_ARRAY, TK_HASH
              emit("    h = ((h << 5) + h) ^ (unsigned int)((uintptr_t)self->#{fname});\n")
            end
          end
          fd = fd.next
        end
        emit("    return h;\n")
        emit("}\n")

        # Equality — field-by-field
        emit("static bool __zn_eq_#{name}(ZnValue a, ZnValue b) {\n")
        emit("    #{name} *pa = (#{name}*)a.as.ptr, *pb = (#{name}*)b.as.ptr;\n")
        emit("    return ")
        fi = 0
        fd = fields
        while fd
          ft = fd.type
          fname = fd.name
          emit(' && ') if fi > 0
          if ft&.kind == TK_STRING
            emit("__zn_val_eq(__zn_val_string(pa->#{fname}), __zn_val_string(pb->#{fname}))")
          elsif ft&.kind == TK_CLASS && ft.name
            emit("pa->#{fname} == pb->#{fname}")
          elsif ft&.kind == TK_STRUCT && ft.name
            emit("({ ZnValue __a, __b; __a.as.ptr = &pa->#{fname}; __b.as.ptr = &pb->#{fname}; __zn_eq_#{ft.name}(__a, __b); })")
          elsif ft && (ft.kind == TK_ARRAY || ft.kind == TK_HASH)
            emit("pa->#{fname} == pb->#{fname}")
          else
            emit("pa->#{fname} == pb->#{fname}")
          end
          fi += 1
          fd = fd.next
        end
        emit('true') if fi == 0
        emit(";\n")
        emit("}\n\n")
      end
    end
  end
end
