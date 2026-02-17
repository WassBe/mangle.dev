const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

/**
 * InputManager - Manages sending requests to other processes and handling responses.
 * 
 * This is an instance-based class - create one instance per request.
 * 
 * @property {Object} response - Complete response with status, data, errors, warnings
 */
class InputManager {
    #process;
    #rawRequest;
    #request;
    #responseData;
    #key;

    /**
     * Generate a unique key for request/response matching.
     * @private
     * @returns {string} Unique key
     */
    static #genKey() {
        return [...Array(32)]
        .map(() => (Math.random() * 256 | 0).toString(16).padStart(2, '0'))
        .join('');
    }

    /**
     * Initialize a new InputManager instance.
     */
    constructor() {
        this.#process = null;
        this.#rawRequest = null;
        this.#request = null;
        this.#responseData = null;
        this.response = null;
        this.#key = null;
    }

    /**
     * Validate file and build command to execute.
     * 
     * @private
     * @param {string} language - Programming language/runtime
     * @param {string} file - Path to file to execute
     * @returns {Array<string>} Command array for subprocess
     * @throws {Error} Invalid file extension, file not found, or permission error
     */
    #getCommand(language, file) {
        const langUpper = String(language).toUpperCase();

        // On Windows, convert forward slashes to backslashes for file system operations
        if (process.platform === 'win32') {
            file = file.replace(/\//g, '\\');
        }

        const fileExt = path.extname(file).toLowerCase();

        // Extension validation - FIRST before file existence check
        const extensionMap = {
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
        };

        if (langUpper in extensionMap) {
            const validExtensions = extensionMap[langUpper];
            if (!validExtensions.includes(fileExt)) {
                const expected = validExtensions.map(ext => ext ? ext : '(no extension)').join(', ');
                throw new Error(`Invalid file '${file}' for language '${language}'. Expected: e.g. 'file${expected}'`);
            }
        }

        // File existence check
        if (!fs.existsSync(file)) {
            throw new Error(`File not found: ${file}`);
        }

        const stats = fs.statSync(file);
        if (!stats.isFile()) {
            throw new Error(`Path is not a file: ${file}`);
        }

        // Permission checks
        const compiledLanguages = ['C', 'CS', 'CPP', 'C#', 'C++', 'CSHARP', 'CPLUSPLUS', 'EXE', 'RUST', 'RS', 'GO', 'GOLANG'];
        if (compiledLanguages.includes(langUpper)) {
            try {
                fs.accessSync(file, fs.constants.X_OK);
            } catch (err) {
                throw new Error(`File is not executable: ${file}`);
            }
        } else {
            try {
                fs.accessSync(file, fs.constants.R_OK);
            } catch (err) {
                throw new Error(`File is not readable: ${file}`);
            }
        }

        // Auto-add ./ for compiled executables if not present and not absolute path
        if (compiledLanguages.includes(langUpper) && !path.isAbsolute(file) && !file.startsWith('./')) {
            file = './' + file;
        }

        // Build command
        const langMap = {
            'PYTHON': ['python', file],
            'PY': ['python', file],
            'JAVASCRIPT': ['node', file],
            'JS': ['node', file],
            'NODE': ['node', file],
            'NODEJS': ['node', file],
            'RUBY': ['ruby', file],
            'RB': ['ruby', file],
            'C': [file],
            'CS': fileExt === '.dll' ? ['dotnet', file] : [file],
            'CPP': [file],
            'C#': fileExt === '.dll' ? ['dotnet', file] : [file],
            'C++': [file],
            'CSHARP': fileExt === '.dll' ? ['dotnet', file] : [file],
            'CPLUSPLUS': [file],
            'EXE': [file],
            'JAR': ['java', '-jar', file],
            'JAVA': ['java', '-jar', file],
            'RUST': [file],
            'RS': [file],
            'GO': fileExt === '.go' ? ['go', 'run', file] : [file],
            'GOLANG': fileExt === '.go' ? ['go', 'run', file] : [file]
        };

        if (!(langUpper in langMap)) {
            throw new Error(`Unsupported language: ${language}`);
        }

        return langMap[langUpper];
    }

    /**
     * Send a request to another process.
     * 
     * @param {boolean} [isUnique=true] - Expect single output (true) or multiple (false)
     * @param {boolean} [optionalOutput=true] - Output is optional (true) or required (false)
     * @param {*} [data=null] - Data to send (any JSON-serializable type)
     * @param {string} language - Target language/runtime
     * @param {string} file - Path to target file
     * 
     * Sets this.response with keys:
     *     - request_status (boolean|null): Success status
     *     - data: Response data (preserves type)
     *     - optionalOutput (boolean): Echo of parameter
     *     - isUnique (boolean): Echo of parameter
     *     - warnings (Array<string>): Warning messages
     *     - errors (Array<string>): Error messages
     */
    request(isUnique = true, optionalOutput = true, data = null, language, file) {
        try {
            this.#key = InputManager.#genKey();
            const command = this.#getCommand(language, file);

            this.#rawRequest = {
                key: this.#key,
                optionalOutput: optionalOutput,
                isUnique: isUnique,
                data: data
            };
            this.#request = JSON.stringify(this.#rawRequest);

            const response = {
                request_status: null,
                data: null,
                optionalOutput: optionalOutput,
                isUnique: isUnique,
                warnings: [],
                errors: []
            };

            const [cmd, ...args] = command;
            const result = spawnSync(cmd, args, {
                input: this.#request,
                encoding: 'utf-8'
            });

            const stdout = result.stdout || '';
            const stderr = result.stderr || '';
            const code = result.status;

            if (code !== 0) {
                response.request_status = false;
                response.errors.push(`Process exited with code ${code}`);
                if (stderr.trim()) {
                    response.errors.push(`stderr: ${stderr.trim()}`);
                }
                response.warnings.push("Warning: these kind of errors result from an error in the targeted script.");
                this.response = response;
                return;
            }

            this.#responseData = [];
            const lines = stdout.trim().split('\n');
            
            for (const line of lines) {
                if (!line.trim()) {
                    continue;
                }
                
                try {
                    const parsedData = JSON.parse(line);
                    
                    // Validate response has matching key or null key (for init errors)
                    // This ensures we only process responses meant for this request
                    if (typeof parsedData === 'object' && 
                        parsedData !== null &&
                        (parsedData.key === this.#key || parsedData.key === null)) {
                        this.#responseData.push(parsedData);
                    }
                } catch (jsonError) {
                    // Ignore lines that aren't valid JSON (e.g., debug prints)
                }
            }

            if (this.#responseData.length !== 0) {
                let failure = false;
                for (const resp of this.#responseData) {
                    if (!resp.request_status) {
                        failure = true;
                    }
                }

                if (!failure) {
                    response.request_status = true;
                } else {
                    response.request_status = false;
                }

                response.isUnique = this.#responseData[0].isUnique;

                for (const resp of this.#responseData) {
                    for (const err of resp.errors) {
                        response.errors.push(err);
                    }
                }

                const dataList = [];
                for (const resp of this.#responseData) {
                    dataList.push(resp.data);
                }

                if (response.isUnique) {
                    if (dataList.length === 1) {
                        response.data = dataList[0];
                    } else {
                        response.request_status = false;
                        response.data = null;
                        response.errors.push(`Error: Expected 1 output (isUnique=True) but received ${dataList.length}.`);
                    }
                } else {
                    response.data = dataList;
                }
            } else if (optionalOutput) {
                response.request_status = null;
                response.data = null;
                response.warnings.push("Warning: the output setting is set to optional, and the targeted program didn't gave any output.");
            } else {
                response.request_status = false;
                response.data = null;
                response.errors.push("Error: OutputManager might not be used or not correctly.");
            }

            this.response = response;

        } catch (e) {
            if (e.message.includes('File not found') || 
                e.message.includes('not executable') || 
                e.message.includes('not readable') ||
                e.message.includes('Invalid file')) {
                this.response = {
                    request_status: false,
                    data: null,
                    optionalOutput: optionalOutput,
                    isUnique: isUnique,
                    warnings: ["Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies."],
                    errors: [`Error: ${e.message}`]
                };
            } else {
                this.response = {
                    request_status: false,
                    data: null,
                    optionalOutput: optionalOutput,
                    isUnique: isUnique,
                    warnings: [],
                    errors: [`Unexpected error: ${e.message}`]
                };
            }
        }
    }

    /**
     * Get the full response object.
     *
     * @returns {Object} The complete response with status, data, errors, warnings.
     */
    getResponse() {
        return this.response;
    }

    /**
     * Get the response data if request was successful.
     *
     * @returns {*} The data from the response (any type), or null if request failed.
     *              The return type matches the type sent by the target process.
     */
    getData() {
        if (this.response) {
            if (this.response.request_status) {
                return this.response.data;
            }
        }
        return null;
    }
}

/**
 * OutputManager - Manages receiving requests from other processes and sending responses.
 * 
 * This is a class-based/static manager - all methods are static.
 * Must call init() before using.
 * 
 * @property {*} data - The request data (accessible after init())
 */
class OutputManager {
    static #originalStdout = null;
    static #request = null;
    static #data = null;
    static #requestStatus = null;
    static #optional = null;
    static #uniqueState = null;
    static #initError = null;
    static #errors = [];
    static #warnings = [];

    static data = null;

    /**
     * Initialize OutputManager and read request from stdin.
     * 
     * Must be called before using output() or accessing data.
     * Suppresses stdout to prevent pollution of JSON protocol.
     */
    static init() {
        // Save original stdout.write function so we can restore it later
        this.#originalStdout = process.stdout.write.bind(process.stdout);
        
        // Replace stdout.write with no-op to suppress all console.log/print statements
        process.stdout.write = () => {};
        
        // Read the entire stdin (the JSON request from InputManager)
        this.#request = fs.readFileSync(0, 'utf-8');
        this.#data = JSON.parse(this.#request);
        this.data = this.#data.data;
        this.#optional = this.#data.optionalOutput;
        
        // Reset state for new request
        this.#errors = [];
        this.#warnings = [];
        this.#initError = null;
        this.#requestStatus = null;
        this.#uniqueState = null;
    }

    /**
     * Internal method to write JSON response to stdout.
     * 
     * @private
     * @param {*} args - Data to send in response
     * @param {Object} _data - Request data for key/metadata
     */
    static #write(args, _data) {
        // Restore original stdout.write to actually output the response
        if (this.#originalStdout) {
            process.stdout.write = this.#originalStdout;
        }
        
        // Build and write JSON response
        const response = JSON.stringify({
            key: _data ? _data.key : null,
            request_status: this.#requestStatus,
            data: args,
            optionalOutput: this.#optional,
            isUnique: _data ? _data.isUnique : null,
            errors: this.#errors,
            warnings: this.#warnings
        });
        
        process.stdout.write(response + '\n');
    }

    /**
     * Send response back to the calling process.
     * 
     * @param {*} val - Data to send (any JSON-serializable type)
     * 
     * Note:
     *     Can be called multiple times if isUnique=false in request.
     *     Will error if called multiple times when isUnique=true.
     */
    static output(val) {
        // Check if OutputManager was initialized
        if (!this.#data) {
            if (!this.#initError) {
                if (this.#originalStdout) {
                    process.stdout.write = this.#originalStdout;
                }

                this.#requestStatus = false;
                this.#errors.push("Error: OutputManager isn't initialized.");
                this.#write(null, this.#data);
                this.#initError = true;
            }
        } else {
            // Check if we can output based on isUnique setting
            // uniqueState tracks if we've already output once
            if (!this.#uniqueState || !this.#data.isUnique) {
                this.#requestStatus = true;
                this.#write(val, this.#data);
            } else {
                // Multiple outputs when isUnique=true is an error
                this.#requestStatus = false;
                this.#errors.push(`Error: outputs out of bound (isUnique: ${this.#uniqueState}).`);
                this.#write(val, this.#data);
            }

            // Mark that we've output once
            this.#uniqueState = this.#data.isUnique;
            
            // Re-suppress stdout after writing response
            process.stdout.write = () => {};
        }
    }
}

module.exports = { InputManager, OutputManager };
