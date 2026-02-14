require 'json'
require 'open3'
require 'securerandom'
require 'stringio'

# InputManager - Manages sending requests to other processes and handling responses.
#
# This is an instance-based class - create one instance per request.
#
# Attributes:
#   response (Hash): Complete response with status, data, errors, warnings
#
# Methods:
#   request(): Send a request to another process
#   get_response(): Get the full response object
#   get_data(): Get the response data (returns nil on error)
class InputManager
  attr_reader :response

  # Generate a unique key for request/response matching.
  # @return [String] Unique key
  def self.gen_key
    SecureRandom.hex(16)
  end

  # Initialize a new InputManager instance.
  def initialize
    @process = nil
    @raw_request = nil
    @request = nil
    @response = nil
    @data = nil
    @key = nil
  end

  private

  # Validate file and build command to execute.
  #
  # @param language [String] Programming language/runtime
  # @param file [String] Path to file to execute
  # @return [Array<String>] Command array for subprocess
  # @raise [ArgumentError] Invalid file extension, file not found, or permission error
  def get_command(language, file)
    lang_upper = language.to_s.upcase

    # On Windows, convert forward slashes to backslashes for file system operations
    if Gem.win_platform?
      file = file.gsub('/', '\\')
    end

    file_ext = File.extname(file).downcase

    # Extension validation - FIRST before file existence check
    extension_map = {
      'PYTHON' => ['.py'],
      'PY' => ['.py'],
      'JAVASCRIPT' => ['.js'],
      'JS' => ['.js'],
      'NODE' => ['.js'],
      'NODEJS' => ['.js'],
      'RUBY' => ['.rb'],
      'RB' => ['.rb'],
      'C' => ['.c', '.out', '.exe', ''],
      'CS' => ['.exe', '.dll', ''],
      'CPP' => ['.cpp', '.cc', '.cxx', '.out', '.exe', ''],
      'C#' => ['.exe', '.dll', ''],
      'C++' => ['.cpp', '.cc', '.cxx', '.out', '.exe', ''],
      'CSHARP' => ['.exe', '.dll', ''],
      'CPLUSPLUS' => ['.cpp', '.cc', '.cxx', '.out', '.exe', ''],
      'EXE' => ['.cpp', '.cc', '.cxx', '.out', '.exe', ''],
      'JAR' => ['.jar'],
      'JAVA' => ['.jar'],
      'RUST' => ['.rs', '.exe', '.out', ''],
      'RS' => ['.rs', '.exe', '.out', ''],
      'GO' => ['.go', '.exe', '.out', ''],
      'GOLANG' => ['.go', '.exe', '.out', '']
    }

    if extension_map.key?(lang_upper)
      valid_extensions = extension_map[lang_upper]
      unless valid_extensions.include?(file_ext)
        expected = valid_extensions.map { |ext| ext.empty? ? '(no extension)' : ext }.join(', ')
        raise ArgumentError, "Invalid file '#{file}' for language '#{language}'. Expected: e.g. 'file#{expected}'"
      end
    end

    # File existence check
    raise ArgumentError, "File not found: #{file}" unless File.exist?(file)
    raise ArgumentError, "Path is not a file: #{file}" unless File.file?(file)

    # Permission checks
    compiled_languages = ['C', 'CS', 'CPP', 'C#', 'C++', 'CSHARP', 'CPLUSPLUS', 'EXE', 'RUST', 'RS', 'GO', 'GOLANG']
    if compiled_languages.include?(lang_upper)
      raise ArgumentError, "File is not executable: #{file}" unless File.executable?(file)
    else
      raise ArgumentError, "File is not readable: #{file}" unless File.readable?(file)
    end

    # Auto-add ./ for compiled executables if not present and not absolute path
    if compiled_languages.include?(lang_upper) && !File.absolute_path?(file) && !file.start_with?('./')
      file = './' + file
    end

    # Build command
    lang_map = {
      'PYTHON' => ['python', file],
      'PY' => ['python', file],
      'JAVASCRIPT' => ['node', file],
      'JS' => ['node', file],
      'NODE' => ['node', file],
      'NODEJS' => ['node', file],
      'RUBY' => ['ruby', file],
      'RB' => ['ruby', file],
      'C' => [file],
      'CS' => file_ext == '.dll' ? ['dotnet', file] : [file],
      'CPP' => [file],
      'C#' => file_ext == '.dll' ? ['dotnet', file] : [file],
      'C++' => [file],
      'CSHARP' => file_ext == '.dll' ? ['dotnet', file] : [file],
      'CPLUSPLUS' => [file],
      'EXE' => [file],
      'JAR' => ['java', '-jar', file],
      'JAVA' => ['java', '-jar', file],
      'RUST' => [file],
      'RS' => [file],
      'GO' => file_ext == '.go' ? ['go', 'run', file] : [file],
      'GOLANG' => file_ext == '.go' ? ['go', 'run', file] : [file]
    }

    raise ArgumentError, "Unsupported language: #{language}" unless lang_map.key?(lang_upper)

    lang_map[lang_upper]
  end

  public

  # Send a request to another process.
  #
  # @param is_unique [Boolean] Expect single output (true) or multiple (false)
  # @param optional_output [Boolean] Output is optional (true) or required (false)
  # @param data [Object] Data to send (any JSON-serializable type)
  # @param language [String] Target language/runtime
  # @param file [String] Path to target file
  #
  # Sets @response (Hash) with keys:
  #     - request_status (Boolean|nil): Success status
  #     - data: Response data (preserves type)
  #     - optionalOutput (Boolean): Echo of parameter
  #     - isUnique (Boolean): Echo of parameter
  #     - warnings (Array<String>): Warning messages
  #     - errors (Array<String>): Error messages
  def request(is_unique: true, optional_output: true, data: nil, language:, file:)
    begin
      @key = InputManager.gen_key
      command = get_command(language, file)

      @raw_request = {
        key: @key,
        optionalOutput: optional_output,
        isUnique: is_unique,
        data: data
      }
      @request = JSON.generate(@raw_request)

      response = {
        request_status: nil,
        data: nil,
        optionalOutput: optional_output,
        isUnique: is_unique,
        warnings: [],
        errors: []
      }

      stdout, stderr, status = Open3.capture3(*command, stdin_data: @request)

      unless status.success?
        response[:request_status] = false
        response[:errors] << "Process exited with code #{status.exitstatus}"
        response[:errors] << "stderr: #{stderr.strip}" unless stderr.strip.empty?
        response[:warnings] << "Warning: these kind of errors result from an error in the targeted script."
        @response = response
        return
      end

      @response_data = []
      stdout.strip.split("\n").each do |line|
        next if line.strip.empty?

        begin
          parsed_data = JSON.parse(line)

          # Validate response has matching key or null key (for init errors)
          # This ensures we only process responses meant for this request
          if parsed_data.is_a?(Hash) && (parsed_data['key'] == @key || parsed_data['key'].nil?)
            @response_data << parsed_data
          end
        rescue JSON::ParserError
          # Ignore lines that aren't valid JSON (e.g., debug prints)
        end
      end

      if @response_data.length != 0
        failure = false
        @response_data.each do |resp|
          failure = true unless resp['request_status']
        end

        response[:request_status] = !failure

        response[:isUnique] = @response_data[0]['isUnique']

        @response_data.each do |resp|
          resp['errors'].each do |err|
            response[:errors] << err
          end
        end

        data_list = []
        @response_data.each do |resp|
          data_list << resp['data']
        end

        if response[:isUnique]
          if data_list.length == 1
            response[:data] = data_list[0]
          else
            response[:request_status] = false
            response[:data] = nil
            response[:errors] << "Error: Expected 1 output (isUnique=True) but received #{data_list.length}."
          end
        else
          response[:data] = data_list
        end
      elsif optional_output
        response[:request_status] = nil
        response[:data] = nil
        response[:warnings] << "Warning: the output setting is set to optional, and the targeted program didn't gave any output."
      else
        response[:request_status] = false
        response[:data] = nil
        response[:errors] << "Error: OutputManager might not be used or not correctly."
      end

      @response = response

    rescue ArgumentError => e
      @response = {
        request_status: false,
        data: nil,
        optionalOutput: optional_output,
        isUnique: is_unique,
        warnings: ["Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies."],
        errors: ["Error: #{e.message}"]
      }
    rescue => e
      @response = {
        request_status: false,
        data: nil,
        optionalOutput: optional_output,
        isUnique: is_unique,
        warnings: [],
        errors: ["Unexpected error: #{e.message}"]
      }
    end
  end

  # Get the full response object.
  #
  # @return [Hash] The complete response with status, data, errors, warnings.
  def get_response
    @response
  end

  # Get the response data if request was successful.
  #
  # @return [Object, nil] The data from the response (any type), or nil if request failed.
  #                       The return type matches the type sent by the target process.
  def get_data
    if @response
      return @response[:data] if @response[:request_status]
    end
    nil
  end

  # Bundle any value for use with request() - for API consistency with other languages.
  # In Ruby, this just returns the value as-is since Ruby handles serialization automatically.
  #
  # @param value [Object] Any value
  # @return [Object] The same value (Ruby handles JSON serialization automatically)
  def self.bundle(value)
    value
  end
end

# OutputManager - Manages receiving requests from other processes and sending responses.
#
# This is a class-based/static manager - all methods are class methods.
# Must call init() before using.
#
# Class Attributes:
#   data: The request data (accessible after init())
#
# Class Methods:
#   init(): Initialize and read request from stdin
#   output(val): Send response back via stdout
class OutputManager
  @@original_stdout = nil
  @@request = nil
  @@data = nil
  @@request_status = nil
  @@optional = nil
  @@unique_state = nil
  @@init_error = nil
  @@errors = []
  @@warnings = []

  class << self
    attr_accessor :data

    # Initialize OutputManager and read request from stdin.
    #
    # Must be called before using output() or accessing data.
    # Suppresses stdout to prevent pollution of JSON protocol.
    def init
      # Save original stdout so we can restore it later
      @@original_stdout = $stdout

      # Redirect stdout to StringIO to suppress all puts/print statements
      $stdout = StringIO.new

      # Read the entire stdin (the JSON request from InputManager)
      @@request = $stdin.read
      @@data = JSON.parse(@@request)
      self.data = @@data['data']
      @@optional = @@data['optionalOutput']

      # Reset state for new request
      @@errors = []
      @@warnings = []
      @@init_error = nil
      @@request_status = nil
      @@unique_state = nil
    end

    # Send response back to the calling process.
    #
    # @param val [Object] Data to send (any JSON-serializable type)
    #
    # Note:
    #   Can be called multiple times if isUnique=false in request.
    #   Will error if called multiple times when isUnique=true.
    def output(val)
      # Check if OutputManager was initialized
      if @@data.nil?
        unless @@init_error
          $stdout = @@original_stdout if @@original_stdout

          @@request_status = false
          @@errors << "Error: OutputManager isn't initialized."
          write_output(nil, @@data)
          @@init_error = true
        end
      else
        # Check if we can output based on isUnique setting
        # unique_state tracks if we've already output once
        if !@@unique_state || !@@data['isUnique']
          @@request_status = true
          write_output(val, @@data)
        else
          # Multiple outputs when isUnique=true is an error
          @@request_status = false
          @@errors << "Error: outputs out of bound (isUnique: #{@@unique_state})."
          write_output(val, @@data)
        end

        # Mark that we've output once
        @@unique_state = @@data['isUnique']

        # Re-suppress stdout after writing response
        $stdout = StringIO.new
      end
    end

    # Bundle any value for use with output() - for API consistency with other languages.
    # In Ruby, this just returns the value as-is since Ruby handles serialization automatically.
    #
    # @param value [Object] Any value
    # @return [Object] The same value (Ruby handles JSON serialization automatically)
    def bundle(value)
      value
    end

    private

    # Internal method to write JSON response to stdout.
    #
    # @param args [Object] Data to send in response
    # @param _data [Hash] Request data for key/metadata
    def write_output(args, _data)
      # Restore original stdout to actually write the response
      $stdout = @@original_stdout if @@original_stdout

      # Build and write JSON response
      response = {
        key: _data ? _data['key'] : nil,
        request_status: @@request_status,
        data: args,
        optionalOutput: @@optional,
        isUnique: _data ? _data['isUnique'] : nil,
        errors: @@errors,
        warnings: @@warnings
      }

      puts JSON.generate(response)
      $stdout.flush
    end
  end
end