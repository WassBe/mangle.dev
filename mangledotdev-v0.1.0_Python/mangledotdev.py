import subprocess
import json
import time
import random
import os
import sys
import io
import uuid


class InputManager:
    """
    Manages sending requests to other processes and handling responses.
    
    This is an instance-based class - create one instance per request.
    
    Attributes:
        response (dict): Complete response with status, data, errors, warnings
    
    Methods:
        request(): Send a request to another process
        get_response(): Get the full response object
        get_data(): Get the response data (returns None on error)
    """
    
    @staticmethod
    def __genKey():
        """Generate a unique key for request/response matching."""
        return uuid.uuid4().hex

    def __init__(self):
        """Initialize a new InputManager instance."""
        self.__process = None
        self.__raw_request = None
        self.__request = None
        self.__response = None
        self.response = None
        self.__data = None
        self.__key = None

    def __get_command(self, language, file):
        """
        Validate file and build command to execute.

        Args:
            language (str): Programming language/runtime
            file (str): Path to file to execute

        Returns:
            list: Command array for subprocess

        Raises:
            ValueError: Invalid file extension for language
            FileNotFoundError: File does not exist
            PermissionError: File not readable/executable
        """
        lang_upper = str(language).upper()

        # On Windows, convert forward slashes to backslashes for file system operations
        if os.name == 'nt':
            file = file.replace('/', '\\')

        file_ext = os.path.splitext(file)[1].lower()

        # Extension validation - FIRST before file existence check
        extension_map = {
            'PYTHON': ['.py'],
            'PY': ['.py'],
            'JAVASCRIPT': ['.js'],
            'JS': ['.js'],
            'NODE': ['.js'],
            'NODEJS': ['.js'],
            'RUBY': ['.rb'],
            'RB': ['.rb'],
            'C': ['.c', '.out', '.exe', ''],
            'CS': ['.exe', '.dll', ''],
            'CPP': ['.cpp', '.cc', '.cxx', '.out', '.exe', ''],
            'C#': ['.exe', '.dll', ''],
            'C++': ['.cpp', '.cc', '.cxx', '.out', '.exe', ''],
            'CSHARP': ['.exe', '.dll', ''],
            'CPLUSPLUS': ['.cpp', '.cc', '.cxx', '.out', '.exe', ''],
            'EXE': ['.cpp', '.cc', '.cxx', '.out', '.exe', ''],
            'JAR': ['.jar'],
            'JAVA': ['.jar'],
            'RUST': ['.rs', '.exe', '.out', ''],
            'RS': ['.rs', '.exe', '.out', ''],
            'GO': ['.go', '.exe', '.out', ''],
            'GOLANG': ['.go', '.exe', '.out', '']
        }

        if lang_upper in extension_map:
            valid_extensions = extension_map[lang_upper]
            if file_ext not in valid_extensions:
                expected = ', '.join([ext if ext else '(no extension)' for ext in valid_extensions])
                raise ValueError(f"Invalid file '{file}' for language '{language}'. Expected: e.g. 'file{expected}'")

        # File existence check
        if not os.path.exists(file):
            raise FileNotFoundError(f"File not found: {file}")

        if not os.path.isfile(file):
            raise ValueError(f"Path is not a file: {file}")

        # Permission checks
        compiled_langs = ['C', 'CS', 'CPP', 'C#', 'C++', 'CSHARP', 'CPLUSPLUS', 'EXE', 'RUST', 'RS', 'GO', 'GOLANG']
        if lang_upper in compiled_langs:
            if not os.access(file, os.X_OK):
                raise PermissionError(f"File is not executable: {file}")
        else:
            if not os.access(file, os.R_OK):
                raise PermissionError(f"File is not readable: {file}")

        # Auto-add ./ for compiled executables if not present and not absolute path
        if lang_upper in compiled_langs and not os.path.isabs(file) and not file.startswith('./'):
            file = './' + file

        # Build command
        lang_map = {
            'PYTHON': ['python', file],
            'PY': ['python', file],
            'JAVASCRIPT': ['node', file],
            'JS': ['node', file],
            'NODE': ['node', file],
            'NODEJS': ['node', file],
            'RUBY': ['ruby', file],
            'RB': ['ruby', file],
            'C': [file],
            'CS': ['dotnet', file] if file_ext == '.dll' else [file],
            'CPP': [file],
            'C#': ['dotnet', file] if file_ext == '.dll' else [file],
            'C++': [file],
            'CSHARP': ['dotnet', file] if file_ext == '.dll' else [file],
            'CPLUSPLUS': [file],
            'EXE': [file],
            'JAR': ['java', '-jar', file],
            'JAVA': ['java', '-jar', file],
            'RUST': [file],
            'RS': [file],
            'GO': ['go', 'run', file] if file_ext == '.go' else [file],
            'GOLANG': ['go', 'run', file] if file_ext == '.go' else [file]
        }

        if lang_upper not in lang_map:
            raise ValueError(f"Unsupported language: {language}")

        return lang_map[lang_upper]

    def request(self, isUnique=True, optionalOutput=True, data: any=None, language: any=str, file: any=str):
        """
        Send a request to another process.
        
        Args:
            isUnique (bool): Expect single output (True) or multiple (False)
            optionalOutput (bool): Output is optional (True) or required (False)
            data: Data to send (any JSON-serializable type)
            language (str): Target language/runtime
            file (str): Path to target file
            
        Sets:
            self.response (dict): Complete response with keys:
                - request_status (bool|None): Success status
                - data: Response data
                - optionalOutput (bool): Echo of parameter
                - isUnique (bool): Echo of parameter
                - warnings (list): Warning messages
                - errors (list): Error messages
        """
        try:
            self.__key = InputManager._InputManager__genKey()
            command = self.__get_command(language, file)

            self.__process = subprocess.Popen(
                command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )

            self.__raw_request = {
                "key": self.__key,
                "optionalOutput": optionalOutput,
                "isUnique": isUnique,
                "data": data
            }
            self.__request = json.dumps(self.__raw_request)

            response = {
                "request_status": None,
                "data": None,
                "optionalOutput": optionalOutput,
                "isUnique": isUnique,
                "warnings": [],
                "errors": []
            }

            output, errors = self.__process.communicate(input=self.__request)

            if self.__process.returncode != 0:
                response["request_status"] = False
                response["errors"].append(f"Process exited with code {self.__process.returncode}")
                if errors.strip():
                    response["errors"].append(f"stderr: {errors.strip()}")
                response["warnings"].append("Warning: these kind of errors result from an error in the targeted script.")
                self.response = response
                return

            self.__response = []
            for line in output.strip().split('\n'):
                if not line.strip():
                    continue
                
                try:
                    __data = json.loads(line)
                    
                    # Validate response has matching key or null key (for init errors)
                    # This ensures we only process responses meant for this request
                    if isinstance(__data, dict) and (__data.get('key') == self.__key or __data.get('key') is None):
                        self.__response.append(__data)
                except json.JSONDecodeError:
                    # Ignore lines that aren't valid JSON (e.g., debug prints)
                    pass

            if len(self.__response) != 0:
                failure = False
                for i in self.__response:
                    if not i["request_status"]:
                        failure = True

                if not failure:
                    response["request_status"] = True
                else:
                    response["request_status"] = False

                response["isUnique"] = self.__response[0]["isUnique"]

                for i in self.__response:
                    for index, e in enumerate(i["errors"]):
                        response["errors"].append(i["errors"][index])
                
                data_list = []
                for i in self.__response:
                    data_list.append(i["data"])
                
                if response["isUnique"]:
                    if len(data_list) == 1:
                        response["data"] = data_list[0]
                    else:
                        response["request_status"] = False
                        response["data"] = None
                        response["errors"].append(f"Error: Expected 1 output (isUnique=True) but received {len(data_list)}.")
                else:
                    response["data"] = data_list
                    
            elif optionalOutput:
                response["request_status"] = None
                response["data"] = None
                response["warnings"].append("Warning: the output setting is set to optional, and the targeted program didn't gave any output.")
            else:
                response["request_status"] = False
                response["data"] = None
                response["errors"].append("Error: OutputManager might not be used or not correctly.")

            self.response = response

        except (FileNotFoundError, PermissionError, ValueError) as e:
            self.response = {
                "request_status": False,
                "data": None,
                "optionalOutput": optionalOutput,
                "isUnique": isUnique,
                "warnings": ["Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies."],
                "errors": [f"Error: {str(e)}"]
            }
        except Exception as e:
            self.response = {
                "request_status": False,
                "data": None,
                "optionalOutput": optionalOutput,
                "isUnique": isUnique,
                "warnings": [],
                "errors": [f"Unexpected error: {str(e)}"]
            }
        return

    def get_response(self):
        """
        Get the full response object.

        Returns:
            dict: The complete response with status, data, errors, warnings.
                  Keys: request_status, data, optionalOutput, isUnique, warnings, errors
        """
        return self.response

    def get_data(self):
        """
        Get the response data if request was successful.

        Returns:
            The data from the response (any type), or None if request failed.
            The return type matches the type sent by the target process.
        """
        if self.response:
            if self.response["request_status"]:
                return self.response["data"]
        return None


class OutputManager:
    """
    Manages receiving requests from other processes and sending responses.
    
    This is a class-based/static manager - all methods are class methods.
    Must call init() before using.
    
    Class Attributes:
        data: The request data (accessible after init())
    
    Class Methods:
        init(): Initialize and read request from stdin
        output(val): Send response back via stdout
    """
    
    __original_stdout = None
    __request = None
    __data = None
    data = None
    __request_status = None
    __optional = None
    __unique_state = None
    __init_error = None
    __errors = []
    __warnings = []

    @classmethod
    def init(cls):
        """
        Initialize OutputManager and read request from stdin.
        
        Must be called before using output() or accessing data.
        Suppresses stdout to prevent pollution of JSON protocol.
        """
        # Save original stdout so we can restore it later
        cls.__original_stdout = sys.stdout
        # Redirect stdout to StringIO to suppress all print statements
        sys.stdout = io.StringIO()
        # Read the entire stdin (the JSON request from InputManager)
        cls.__request = sys.stdin.read()
        cls.__data = json.loads(cls.__request)
        cls.data = cls.__data["data"]
        cls.__optional = cls.__data["optionalOutput"]
        
        # Reset state for new request
        cls.__errors = []
        cls.__warnings = []
        cls.__init_error = None
        cls.__request_status = None
        cls.__unique_state = None

    @classmethod
    def __write(cls, args, _data):
        """
        Internal method to write JSON response to stdout.
        
        Args:
            args: Data to send in response
            _data: Request data for key/metadata
        """
        # Restore original stdout to actually write the response
        if cls.__original_stdout:
            sys.stdout = cls.__original_stdout
        else:
            sys.stdout = sys.__stdout__
        
        # Build and write JSON response
        response = json.dumps({
            "key": _data["key"] if _data else None,
            "request_status": cls.__request_status, 
            "data": args,
            "optionalOutput": cls.__optional,
            "isUnique": _data["isUnique"] if _data else None,
            "errors": cls.__errors, 
            "warnings": cls.__warnings
        })
        
        sys.stdout.write(response)
        sys.stdout.write("\n")
        sys.stdout.flush()

    @classmethod
    def output(cls, val):
        """
        Send response back to the calling process.
        
        Args:
            val: Data to send (any JSON-serializable type)
            
        Note:
            Can be called multiple times if isUnique=False in request.
            Will error if called multiple times when isUnique=True.
        """
        # Check if OutputManager was initialized
        if not cls.__data:
            if not cls.__init_error:
                if cls.__original_stdout:
                    sys.stdout = cls.__original_stdout
                else:
                    sys.stdout = sys.__stdout__

                cls.__request_status = False
                cls.__errors.append("Error: OutputManager isn't initialized.")
                cls._OutputManager__write(args=None, _data=cls.__data)
                cls.__init_error = True

        else:
            # Check if we can output based on isUnique setting
            # unique_state tracks if we've already output once
            if not cls.__unique_state or not cls.__data["isUnique"]:
                cls.__request_status = True
                cls._OutputManager__write(args=val, _data=cls.__data)
            else:
                # Multiple outputs when isUnique=True is an error
                cls.__request_status = False
                cls.__errors.append(f"Error: outputs out of bound (isUnique: {cls.__unique_state}).")
                cls._OutputManager__write(args=val, _data=cls.__data)

            # Mark that we've output once
            cls.__unique_state = cls.__data["isUnique"]
            # Re-suppress stdout after writing response
            sys.stdout = io.StringIO()