# frozen_string_literal: true

module Zinc
  # Operator kinds
  module Op
    ADD = :add; SUB = :sub; MUL = :mul; DIV = :div; MOD = :mod
    EQ = :eq; NE = :ne; LT = :lt; GT = :gt; LE = :le; GE = :ge
    AND = :and; OR = :or
    NOT = :not; NEG = :neg; POS = :pos
    INC = :inc; DEC = :dec
    ASSIGN = :assign
    ADD_ASSIGN = :add_assign; SUB_ASSIGN = :sub_assign
    MUL_ASSIGN = :mul_assign; DIV_ASSIGN = :div_assign; MOD_ASSIGN = :mod_assign

    STR = {
      add: '+', sub: '-', mul: '*', div: '/', mod: '%',
      eq: '==', ne: '!=', lt: '<', gt: '>', le: '<=', ge: '>=',
      and: '&&', or: '||',
      not: '!', neg: '-', pos: '+',
      inc: '++', dec: '--',
      assign: '=',
      add_assign: '+=', sub_assign: '-=',
      mul_assign: '*=', div_assign: '/=', mod_assign: '%=',
    }.freeze

    def self.to_s(op)
      STR[op] || '?'
    end
  end

  # Type kinds
  TK_UNKNOWN = :unknown
  TK_INT     = :int
  TK_FLOAT   = :float
  TK_STRING  = :string
  TK_BOOL    = :bool
  TK_CHAR    = :char
  TK_VOID    = :void
  TK_STRUCT  = :struct
  TK_CLASS   = :class
  TK_ARRAY   = :array
  TK_HASH    = :hash

  # Resolved type representation
  class Type
    attr_accessor :kind, :is_optional, :name, :elem, :key

    def initialize(kind = TK_UNKNOWN)
      @kind = kind
      @is_optional = false
      @name = nil
      @elem = nil
      @key = nil
    end

    def clone
      t = Type.new(@kind)
      t.is_optional = @is_optional
      t.name = @name&.dup
      t.elem = @elem&.clone
      t.key = @key&.clone
      t
    end

    def ==(other)
      return true if equal?(other)
      return false unless other.is_a?(Type)
      return false if @kind != other.kind
      return false if @is_optional != other.is_optional
      if @kind == TK_STRUCT || @kind == TK_CLASS
        return @name == other.name
      end
      return false unless @elem == other.elem
      return false unless @key == other.key
      true
    end
  end

  # Type info field (for object/tuple type specs)
  TypeInfoField = Struct.new(:name, :type, :next) do
    def initialize(name = nil, type = nil, next_field = nil)
      super(name, type, next_field)
    end
  end

  # Parser-side type specification
  class TypeInfo
    attr_accessor :kind, :is_optional, :name, :fields, :is_object, :is_tuple, :elem, :key

    def initialize(kind = TK_UNKNOWN)
      @kind = kind
      @is_optional = false
      @name = nil
      @fields = nil
      @is_object = false
      @is_tuple = false
      @elem = nil
      @key = nil
    end

    def to_type
      t = Type.new(@kind)
      t.is_optional = @is_optional
      t.name = @name&.dup
      t.elem = @elem&.to_type
      t.key = @key&.to_type
      t
    end
  end

  # Helper to append TypeInfoField linked lists
  def self.type_info_field_append(list, field)
    return field unless list
    cur = list
    cur = cur.next while cur.next
    cur.next = field
    list
  end

  module AST
    # Base class for all AST nodes
    class Node
      attr_accessor :line, :string_id, :is_fresh_alloc, :resolved_type

      def initialize
        @line = 0
        @string_id = -1
        @is_fresh_alloc = false
        @resolved_type = nil
      end

      def print_ast(indent = 0)
        # Override in subclasses
      end

      private

      def indent_print(indent)
        print '  ' * indent
      end

      def print_type_info(ti)
        unless ti
          print '(inferred)'
          return
        end
        case ti.kind
        when TK_INT    then print 'int'
        when TK_FLOAT  then print 'float'
        when TK_STRING then print 'String'
        when TK_BOOL   then print 'bool'
        when TK_CHAR   then print 'char'
        when TK_VOID   then print 'void'
        when TK_STRUCT
          print(ti.name || 'struct')
        when TK_CLASS
          print(ti.name || 'class')
        else
          print 'unknown'
        end
        print '?' if ti.is_optional
      end
    end

    class Program < Node
      attr_accessor :stmts
      def initialize(stmts)
        super()
        @stmts = stmts || []
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'Program'
        @stmts.each { |s| s.print_ast(indent + 1) }
      end
    end

    class Block < Node
      attr_accessor :stmts
      def initialize(stmts)
        super()
        @stmts = stmts || []
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'Block'
        @stmts.each { |s| s.print_ast(indent + 1) }
      end
    end

    class IntLit < Node
      attr_accessor :value
      def initialize(val)
        super()
        @value = val
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "Int: #{@value}"
      end
    end

    class FloatLit < Node
      attr_accessor :value
      def initialize(val)
        super()
        @value = val
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "Float: #{format_float(@value)}"
      end

      private

      def format_float(v)
        # Match C's %g format
        s = sprintf('%g', v)
        s
      end
    end

    class StringLit < Node
      attr_accessor :value
      def initialize(val)
        super()
        @value = val
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "String: \"#{@value}\""
      end
    end

    class BoolLit < Node
      attr_accessor :value
      def initialize(val)
        super()
        @value = val
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "Bool: #{@value ? 'true' : 'false'}"
      end
    end

    class CharLit < Node
      attr_accessor :value
      def initialize(val)
        super()
        @value = val
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "Char: '#{@value}'"
      end
    end

    class Ident < Node
      attr_accessor :name
      def initialize(name)
        super()
        @name = name
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "Ident: #{@name}"
      end
    end

    class Param < Node
      attr_accessor :name, :type_info
      def initialize(name, type_info)
        super()
        @name = name
        @type_info = type_info
      end

      def print_ast(indent = 0)
        indent_print(indent)
        print "Param: #{@name}: "
        print_type_info(@type_info)
        puts
      end
    end

    class BinOp < Node
      attr_accessor :left, :op, :right
      def initialize(left, op, right)
        super()
        @left = left
        @op = op
        @right = right
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "BinOp: #{Op.to_s(@op)}"
        @left.print_ast(indent + 1)
        @right.print_ast(indent + 1)
      end
    end

    class UnaryOp < Node
      attr_accessor :op, :operand
      def initialize(op, operand)
        super()
        @op = op
        @operand = operand
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "UnaryOp: #{Op.to_s(@op)}"
        @operand.print_ast(indent + 1)
      end
    end

    class Assign < Node
      attr_accessor :target, :value
      def initialize(target, value)
        super()
        @target = target
        @value = value
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'Assign'
        indent_print(indent + 1); puts 'Target:'
        @target.print_ast(indent + 2)
        indent_print(indent + 1); puts 'Value:'
        @value.print_ast(indent + 2)
      end
    end

    class CompoundAssign < Node
      attr_accessor :target, :op, :value
      def initialize(target, op, value)
        super()
        @target = target
        @op = op
        @value = value
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "CompoundAssign: #{Op.to_s(@op)}"
        indent_print(indent + 1); puts 'Target:'
        @target.print_ast(indent + 2)
        indent_print(indent + 1); puts 'Value:'
        @value.print_ast(indent + 2)
      end
    end

    class IncDec < Node
      attr_accessor :target, :op, :is_prefix
      def initialize(target, op, is_prefix)
        super()
        @target = target
        @op = op
        @is_prefix = is_prefix
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "IncDec: #{Op.to_s(@op)} #{@is_prefix ? 'prefix' : 'postfix'}"
        @target.print_ast(indent + 1)
      end
    end

    class Decl < Node
      attr_accessor :name, :value, :is_const
      def initialize(name, value, is_const)
        super()
        @name = name
        @value = value
        @is_const = is_const
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "#{@is_const ? 'LetDecl' : 'VarDecl'}: #{@name}"
        @value.print_ast(indent + 1)
      end
    end

    class If < Node
      attr_accessor :cond, :then_b, :else_b
      def initialize(cond, then_b, else_b)
        super()
        @cond = cond
        @then_b = then_b
        @else_b = else_b
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'If'
        indent_print(indent + 1); puts 'Cond:'
        @cond.print_ast(indent + 2)
        indent_print(indent + 1); puts 'Then:'
        @then_b.print_ast(indent + 2)
        if @else_b
          indent_print(indent + 1); puts 'Else:'
          @else_b.print_ast(indent + 2)
        end
      end
    end

    class While < Node
      attr_accessor :cond, :body
      def initialize(cond, body)
        super()
        @cond = cond
        @body = body
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'While'
        indent_print(indent + 1); puts 'Cond:'
        @cond.print_ast(indent + 2)
        indent_print(indent + 1); puts 'Body:'
        @body.print_ast(indent + 2)
      end
    end

    class For < Node
      attr_accessor :init, :cond, :update, :body
      def initialize(init, cond, update, body)
        super()
        @init = init
        @cond = cond
        @update = update
        @body = body
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'For'
        indent_print(indent + 1); puts 'Init:'
        @init&.print_ast(indent + 2)
        indent_print(indent + 1); puts 'Cond:'
        @cond&.print_ast(indent + 2)
        indent_print(indent + 1); puts 'Update:'
        @update&.print_ast(indent + 2)
        indent_print(indent + 1); puts 'Body:'
        @body&.print_ast(indent + 2)
      end
    end

    class Break < Node
      attr_accessor :value
      def initialize(value = nil)
        super()
        @value = value
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'Break'
        @value&.print_ast(indent + 1)
      end
    end

    class Continue < Node
      attr_accessor :value
      def initialize(value = nil)
        super()
        @value = value
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'Continue'
        @value&.print_ast(indent + 1)
      end
    end

    class FuncDef < Node
      attr_accessor :name, :params, :return_type, :body
      def initialize(name, params, body)
        super()
        @name = name
        @params = params || []
        @return_type = nil
        @body = body
      end

      def print_ast(indent = 0)
        indent_print(indent)
        print "FuncDef: #{@name}("
        @params.each_with_index do |p, i|
          if p.is_a?(Param)
            print "#{p.name}: "
            print_type_info(p.type_info)
          end
          print ', ' if i < @params.size - 1
        end
        puts ')'
        @body.print_ast(indent + 1)
      end
    end

    class Call < Node
      attr_accessor :name, :args, :is_struct_init
      def initialize(name, args)
        super()
        @name = name
        @args = args || []
        @is_struct_init = false
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "Call: #{@name}"
        @args.each { |a| a.print_ast(indent + 1) }
      end
    end

    class Return < Node
      attr_accessor :value
      def initialize(value = nil)
        super()
        @value = value
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'Return'
        @value&.print_ast(indent + 1)
      end
    end

    class FieldAccess < Node
      attr_accessor :object, :field, :is_dot_int
      def initialize(object, field)
        super()
        @object = object
        @field = field
        @is_dot_int = false
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "FieldAccess: .#{@field}"
        @object.print_ast(indent + 1)
      end
    end

    class Index < Node
      attr_accessor :object, :index
      def initialize(object, index)
        super()
        @object = object
        @index = index
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'Index'
        @object.print_ast(indent + 1)
        @index.print_ast(indent + 1)
      end
    end

    class OptionalCheck < Node
      attr_accessor :operand
      def initialize(operand)
        super()
        @operand = operand
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'OptionalCheck'
        @operand.print_ast(indent + 1)
      end
    end

    class TypeDef < Node
      attr_accessor :name, :fields, :is_class
      def initialize(name, fields, is_class)
        super()
        @name = name
        @fields = fields || []
        @is_class = is_class
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "#{@is_class ? 'ClassDef' : 'StructDef'}: #{@name}"
        @fields.each { |f| f.print_ast(indent + 1) }
      end
    end

    class StructField < Node
      attr_accessor :name, :type_info, :default_value, :is_const, :is_weak
      def initialize(name, type_info, default_value, is_const)
        super()
        @name = name
        @type_info = type_info
        @default_value = default_value
        @is_const = is_const
        @is_weak = false
      end

      def print_ast(indent = 0)
        indent_print(indent)
        print "StructField: #{@is_const ? 'let ' : 'var '}#{@name}"
        if @type_info
          print ': '
          print_type_info(@type_info)
        end
        puts
        @default_value&.print_ast(indent + 1)
      end
    end

    class NamedArg < Node
      attr_accessor :name, :value
      def initialize(name, value)
        super()
        @name = name
        @value = value
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts "NamedArg: #{@name}"
        @value.print_ast(indent + 1)
      end
    end

    class Tuple < Node
      attr_accessor :elements
      def initialize(elements)
        super()
        @elements = elements || []
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'Tuple'
        @elements.each { |e| e.print_ast(indent + 1) }
      end
    end

    class ObjectLiteral < Node
      attr_accessor :fields
      def initialize(fields)
        super()
        @fields = fields || []
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'ObjectLiteral'
        @fields.each { |f| f.print_ast(indent + 1) }
      end
    end

    class ArrayLiteral < Node
      attr_accessor :elems
      def initialize(elems)
        super()
        @elems = elems || []
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'ArrayLiteral'
        @elems.each { |e| e.print_ast(indent + 1) }
      end
    end

    class HashLiteral < Node
      attr_accessor :pairs
      def initialize(pairs)
        super()
        @pairs = pairs || []
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'HashLiteral'
        @pairs.each { |p| p.print_ast(indent + 1) }
      end
    end

    class HashPair < Node
      attr_accessor :key, :value
      def initialize(key, value)
        super()
        @key = key
        @value = value
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'HashPair'
        @key.print_ast(indent + 1)
        @value.print_ast(indent + 1)
      end
    end

    class ExternBlock < Node
      attr_accessor :decls
      def initialize(decls)
        super()
        @decls = decls || []
      end

      def print_ast(indent = 0)
        indent_print(indent)
        puts 'ExternBlock'
        @decls.each { |d| d.print_ast(indent + 1) }
      end
    end

    class ExternFunc < Node
      attr_accessor :name, :params, :return_type
      def initialize(name, params, return_type)
        super()
        @name = name
        @params = params || []
        @return_type = return_type
      end

      def print_ast(indent = 0)
        indent_print(indent)
        print "ExternFunc: #{@name}("
        @params.each_with_index do |p, i|
          print "#{p.name}: "
          print_type_info(p.type_info)
          print ', ' if i < @params.size - 1
        end
        print ')'
        if @return_type
          print ' -> '
          print_type_info(@return_type)
        end
        puts
      end
    end

    class ExternVar < Node
      attr_accessor :name, :type_info
      def initialize(name, type_info)
        super()
        @name = name
        @type_info = type_info
      end

      def print_ast(indent = 0)
        indent_print(indent)
        print "ExternVar: #{@name}: "
        print_type_info(@type_info)
        puts
      end
    end

    class ExternLet < Node
      attr_accessor :name, :type_info
      def initialize(name, type_info)
        super()
        @name = name
        @type_info = type_info
      end

      def print_ast(indent = 0)
        indent_print(indent)
        print "ExternLet: #{@name}: "
        print_type_info(@type_info)
        puts
      end
    end

    class TypedEmptyArray < Node
      attr_accessor :elem_type, :elem_name
      def initialize(elem_type, elem_name = nil)
        super()
        @elem_type = elem_type
        @elem_name = elem_name
      end

      def print_ast(indent = 0)
        indent_print(indent)
        tk_int = { unknown: 0, int: 1, float: 2, string: 3, bool: 4, char: 5,
                   void: 6, struct: 7, class: 8, array: 9, hash: 10 }
        puts "TypedEmptyArray: elem=#{tk_int[@elem_type] || 0}"
      end
    end

    class TypedEmptyHash < Node
      attr_accessor :key_type, :value_type, :value_name
      def initialize(key_type, value_type, value_name = nil)
        super()
        @key_type = key_type
        @value_type = value_type
        @value_name = value_name
      end

      def print_ast(indent = 0)
        indent_print(indent)
        tk_int = { unknown: 0, int: 1, float: 2, string: 3, bool: 4, char: 5,
                   void: 6, struct: 7, class: 8, array: 9, hash: 10 }
        puts "TypedEmptyHash: key=#{tk_int[@key_type] || 0} val=#{tk_int[@value_type] || 0}"
      end
    end
  end
end
