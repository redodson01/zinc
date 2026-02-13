# frozen_string_literal: true

require 'strscan'

module Zinc
  class Scanner
    KEYWORDS = {
      'let' => :LET, 'var' => :VAR,
      'if' => :IF, 'unless' => :UNLESS, 'else' => :ELSE,
      'while' => :WHILE, 'until' => :UNTIL, 'for' => :FOR,
      'break' => :BREAK, 'continue' => :CONTINUE,
      'func' => :FUNC, 'return' => :RETURN,
      'extern' => :EXTERN, 'struct' => :STRUCT, 'class' => :CLASS, 'weak' => :WEAK,
      'true' => :BOOL_LIT, 'false' => :BOOL_LIT,
      'int' => :TYPE_INT, 'float' => :TYPE_FLOAT,
      'String' => :TYPE_STRING, 'bool' => :TYPE_BOOL, 'char' => :TYPE_CHAR,
    }.freeze

    def initialize(source)
      @ss = StringScanner.new(source)
      @line = 1
      @state = :initial  # :initial or :str
      @brace_depth = 0
      @str_buf = +''
      @had_interpolation = false
      @tokens = []
      tokenize
    end

    def tokens
      @tokens
    end

    private

    def tokenize
      until @ss.eos?
        if @state == :str
          scan_str
        else
          scan_initial
        end
      end
    end

    def scan_initial
      # Skip whitespace (not newlines — newlines are statement separators but
      # Bison grammar treats them as whitespace; we do the same)
      if @ss.scan(/[ \t]+/)
        return
      end

      if @ss.scan(/\n/)
        @line += 1
        return
      end

      # Comments
      if @ss.scan(/#[^\n]*/)
        return
      end

      # Hex integer
      if @ss.scan(/0[xX][0-9a-fA-F]+/)
        @tokens << [:INT_LIT, @ss.matched.to_i(16), @line]
        return
      end

      # Float literal
      if @ss.scan(/[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/)
        @tokens << [:FLOAT_LIT, @ss.matched.to_f, @line]
        return
      end

      # Int with scientific notation (treated as int in C version)
      if @ss.scan(/[0-9]+[eE][+-]?[0-9]+/)
        @tokens << [:INT_LIT, @ss.matched.to_f.to_i, @line]
        return
      end

      # Integer literal
      if @ss.scan(/[0-9]+/)
        @tokens << [:INT_LIT, @ss.matched.to_i, @line]
        return
      end

      # String literal start
      if @ss.scan(/"/)
        @str_buf = +''
        @had_interpolation = false
        @state = :str
        return
      end

      # Char literal with escape
      if @ss.scan(/'\\(.)'/)
        ch = case @ss[1]
             when 'n' then "\n"
             when 't' then "\t"
             when 'r' then "\r"
             when '0' then "\0"
             when "'" then "'"
             when '\\' then '\\'
             else @ss[1]
             end
        @tokens << [:CHAR_LIT, ch, @line]
        return
      end

      # Char literal
      if @ss.scan(/'([^'\\])'/)
        @tokens << [:CHAR_LIT, @ss[1], @line]
        return
      end

      # Keywords and identifiers
      if @ss.scan(/[a-zA-Z_][a-zA-Z0-9_]*/)
        word = @ss.matched
        if KEYWORDS.key?(word)
          tok = KEYWORDS[word]
          if tok == :BOOL_LIT
            @tokens << [:BOOL_LIT, word == 'true', @line]
          else
            @tokens << [tok, word, @line]
          end
        else
          @tokens << [:IDENTIFIER, word, @line]
        end
        return
      end

      # Multi-char operators (order matters)
      if @ss.scan(/->/)  then @tokens << [:ARROW, '->', @line]; return; end
      if @ss.scan(/\+=/) then @tokens << [:PLUS_ASSIGN, '+=', @line]; return; end
      if @ss.scan(/-=/)  then @tokens << [:MINUS_ASSIGN, '-=', @line]; return; end
      if @ss.scan(/\*=/) then @tokens << [:STAR_ASSIGN, '*=', @line]; return; end
      if @ss.scan(/\/=/) then @tokens << [:SLASH_ASSIGN, '/=', @line]; return; end
      if @ss.scan(/%=/)  then @tokens << [:PERCENT_ASSIGN, '%=', @line]; return; end
      if @ss.scan(/\+\+/) then @tokens << [:INCREMENT, '++', @line]; return; end
      if @ss.scan(/--/)  then @tokens << [:DECREMENT, '--', @line]; return; end
      if @ss.scan(/==/)  then @tokens << [:EQ, '==', @line]; return; end
      if @ss.scan(/!=/)  then @tokens << [:NE, '!=', @line]; return; end
      if @ss.scan(/<=/)  then @tokens << [:LE, '<=', @line]; return; end
      if @ss.scan(/>=/)  then @tokens << [:GE, '>=', @line]; return; end
      if @ss.scan(/&&/)  then @tokens << [:AND, '&&', @line]; return; end
      if @ss.scan(/\|\|/) then @tokens << [:OR, '||', @line]; return; end

      # Single-char tokens
      if @ss.scan(/\(/) then @tokens << [:LPAREN, '(', @line]; return; end
      if @ss.scan(/\)/) then @tokens << [:RPAREN, ')', @line]; return; end
      if @ss.scan(/\{/)
        @brace_depth += 1 if @brace_depth > 0
        @tokens << [:LBRACE, '{', @line]
        return
      end
      if @ss.scan(/\}/)
        if @brace_depth > 0
          @brace_depth -= 1
          if @brace_depth == 0
            # Return to string mode
            @str_buf = +''
            @state = :str
            return
          end
        end
        @tokens << [:RBRACE, '}', @line]
        return
      end
      if @ss.scan(/\[/) then @tokens << [:LBRACKET, '[', @line]; return; end
      if @ss.scan(/\]/) then @tokens << [:RBRACKET, ']', @line]; return; end
      if @ss.scan(/,/)  then @tokens << [:COMMA, ',', @line]; return; end
      if @ss.scan(/;/)  then @tokens << [:SEMICOLON, ';', @line]; return; end
      if @ss.scan(/:/)  then @tokens << [:COLON, ':', @line]; return; end
      if @ss.scan(/\+/) then @tokens << [:PLUS, '+', @line]; return; end
      if @ss.scan(/-/)  then @tokens << [:MINUS, '-', @line]; return; end
      if @ss.scan(/\*/) then @tokens << [:STAR, '*', @line]; return; end
      if @ss.scan(/\//) then @tokens << [:SLASH, '/', @line]; return; end
      if @ss.scan(/%/)  then @tokens << [:PERCENT, '%', @line]; return; end
      if @ss.scan(/</)  then @tokens << [:LT, '<', @line]; return; end
      if @ss.scan(/>/)  then @tokens << [:GT, '>', @line]; return; end
      if @ss.scan(/=/)  then @tokens << [:ASSIGN, '=', @line]; return; end
      if @ss.scan(/!/)  then @tokens << [:NOT, '!', @line]; return; end
      if @ss.scan(/\./) then @tokens << [:DOT, '.', @line]; return; end
      if @ss.scan(/\?/) then @tokens << [:QUESTION, '?', @line]; return; end

      # Unknown character
      ch = @ss.getch
      $stderr.puts "Unknown char '#{ch}' at line #{@line}"
    end

    def scan_str
      loop do
        break if @ss.eos?

        # Escape sequences
        if @ss.scan(/\\n/)  then @str_buf << "\n"; next; end
        if @ss.scan(/\\t/)  then @str_buf << "\t"; next; end
        if @ss.scan(/\\r/)  then @str_buf << "\r"; next; end
        if @ss.scan(/\\\\/) then @str_buf << '\\'; next; end
        if @ss.scan(/\\"/)  then @str_buf << '"'; next; end
        if @ss.scan(/\\\$/) then @str_buf << '$'; next; end
        if @ss.scan(/\\0/)  then @str_buf << "\0"; next; end

        # Interpolation start
        if @ss.scan(/\$\{/)
          @had_interpolation = true
          @tokens << [:STRING_PART, @str_buf, @line]
          @str_buf = +''
          @brace_depth = 1
          @state = :initial
          return
        end

        # String end
        if @ss.scan(/"/)
          @state = :initial
          if @had_interpolation
            @tokens << [:STRING_TAIL, @str_buf, @line]
          else
            @tokens << [:STRING_LIT, @str_buf, @line]
          end
          @str_buf = +''
          return
        end

        # Newline in string
        if @ss.scan(/\n/)
          @line += 1
          @str_buf << "\n"
          next
        end

        # Any other character
        if @ss.scan(/./)
          @str_buf << @ss.matched
          next
        end
      end
    end
  end
end
