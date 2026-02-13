# frozen_string_literal: true

require 'tmpdir'

ZINC = File.join(__dir__, 'bin', 'zinc')
TEST_PASS_DIR = 'test/pass'
TEST_FAIL_DIR = 'test/fail'
TEST_LEAK_DIR = 'test/leak'

desc 'Compile parser.ry → parser.rb via racc'
task :build do
  sh 'racc -o lib/zinc/parser.rb lib/zinc/parser.ry'
end

desc 'Run pass + fail tests'
task :test do
  passed = 0
  failed = 0

  puts ''
  puts '========================================'
  puts 'Running valid program tests...'
  puts '========================================'
  Dir["#{TEST_PASS_DIR}/*.zn"].sort.each do |f|
    name = File.basename(f)
    if system("ruby #{ZINC} --ast #{f} > /dev/null 2>&1")
      puts "  PASS: #{name}"
      passed += 1
    else
      puts "  FAIL: #{name} (expected to parse successfully)"
      failed += 1
    end
  end

  puts ''
  puts '========================================'
  puts 'Running error recovery tests...'
  puts '========================================'
  Dir["#{TEST_FAIL_DIR}/*.zn"].sort.each do |f|
    name = File.basename(f)
    expected = File.open(f) { |fh| fh.readline }[/# ERRORS: (\d+)/, 1].to_i
    output = `ruby #{ZINC} --check #{f} 2>&1`
    exitcode = $?.exitstatus
    parse_errs = output[/(\d+) parse error/, 1].to_i
    sem_errs = output[/(\d+) semantic error/, 1].to_i
    actual = parse_errs + sem_errs

    if exitcode == 1 && actual == expected
      puts "  PASS: #{name} (#{actual} errors as expected)"
      passed += 1
    elsif exitcode == 0
      puts "  FAIL: #{name} (parsed successfully, expected #{expected} errors)"
      failed += 1
    else
      puts "  FAIL: #{name} (got #{actual} errors, expected #{expected})"
      failed += 1
    end
  end

  puts ''
  puts '========================================'
  puts "Test Summary: #{passed} passed, #{failed} failed"
  puts '========================================'
  exit 1 if failed > 0
end

desc 'Compile and run each pass test'
task :'test-transpile' do
  passed = 0
  failed = 0
  tmpdir = Dir.mktmpdir

  puts ''
  puts '========================================'
  puts 'Running transpiler tests...'
  puts '========================================'
  Dir["#{TEST_PASS_DIR}/*.zn"].sort.each do |f|
    name = File.basename(f, '.zn')
    out = File.join(tmpdir, name)
    if system("ruby #{ZINC} -c #{f} -o #{out} > /dev/null 2>&1")
      if File.executable?(out)
        if system("#{out} > /dev/null 2>&1")
          puts "  PASS: #{name} (transpiled, compiled, ran)"
          passed += 1
        else
          puts "  FAIL: #{name} (runtime error)"
          failed += 1
        end
      else
        puts "  FAIL: #{name} (executable not created)"
        failed += 1
      end
    else
      puts "  FAIL: #{name} (transpilation/compilation failed)"
      failed += 1
    end
  end
  puts ''
  puts '========================================'
  puts "Transpiler Summary: #{passed} passed, #{failed} failed"
  puts '========================================'
  exit 1 if failed > 0
end

desc 'Run leak tests (macOS only)'
task :'test-leaks' do
  unless system('command -v leaks > /dev/null 2>&1')
    puts ''
    puts '========================================'
    puts 'Skipping leak tests (leaks command not available)'
    puts '========================================'
    exit 0
  end

  passed = 0
  failed = 0
  tmpdir = Dir.mktmpdir

  puts ''
  puts '========================================'
  puts 'Running leak tests...'
  puts '========================================'
  Dir["#{TEST_LEAK_DIR}/*.zn"].sort.each do |f|
    name = File.basename(f, '.zn')
    out = File.join(tmpdir, name)
    if system("ruby #{ZINC} -c #{f} -o #{out} > /dev/null 2>&1")
      if File.executable?(out)
        output = `leaks --atExit -- #{out} 2>&1`
        if output.include?('0 leaks for 0 total leaked bytes')
          puts "  PASS: #{name} (0 leaks)"
          passed += 1
        else
          leak_count = output[/(\d+) leaks for/, 1]
          puts "  FAIL: #{name} (#{leak_count} leaks detected)"
          failed += 1
        end
      else
        puts "  FAIL: #{name} (executable not created)"
        failed += 1
      end
    else
      puts "  FAIL: #{name} (transpilation/compilation failed)"
      failed += 1
    end
  end
  puts ''
  puts '========================================'
  puts "Leak Test Summary: #{passed} passed, #{failed} failed"
  puts '========================================'
  exit 1 if failed > 0
end

desc 'Run all tests'
task :'test-all' => [:test, :'test-transpile', :'test-leaks']
