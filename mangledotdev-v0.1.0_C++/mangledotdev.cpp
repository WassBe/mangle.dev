#include "mangledotdev.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <sys/stat.h>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <io.h>
#include <process.h>
#define popen _popen
#define pclose _pclose
#define access _access
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define F_OK 0
#define R_OK 4
#define X_OK 1
// Note: don't define close as _close - it conflicts with std::fstream::close()
// Use _close() directly for file descriptors
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

extern "C" {
#include "cJSON.h"
}

// Initialize static members of OutputManager
int OutputManager::original_stdout_fd = -1;
std::string OutputManager::request_json = "";
std::string OutputManager::key = "";
std::string OutputManager::data = "";
bool OutputManager::optional_output = true;
bool OutputManager::is_unique = true;
bool OutputManager::request_status = false;
bool OutputManager::request_status_set = false;
bool OutputManager::unique_state = false;
bool OutputManager::unique_state_set = false;
bool OutputManager::init_error = false;
std::vector<std::string> OutputManager::errors;
std::vector<std::string> OutputManager::warnings;

/**
 * Generate a unique key for request/response matching
 *
 * Returns:
 *     Unique key as 32-char hex string
 */
static std::string genKey() {
    unsigned char bytes[16];
#ifdef _WIN32
    HCRYPTPROV hProv;
    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    CryptGenRandom(hProv, 16, bytes);
    CryptReleaseContext(hProv, 0);
#else
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        fread(bytes, 1, 16, f);
        fclose(f);
    } else {
        srand(static_cast<unsigned int>(time(nullptr) ^ clock()));
        for (int i = 0; i < 16; i++) bytes[i] = rand() % 256;
    }
#endif
    char hex[33];
    for (int i = 0; i < 16; i++) {
        sprintf(hex + i * 2, "%02x", bytes[i]);
    }
    hex[32] = '\0';
    return std::string(hex);
}

/**
 * Get file extension from filename
 * 
 * Parameters:
 *     filename: Path to file
 * 
 * Returns:
 *     Extension string (e.g., ".py") or empty string
 */
static std::string getExtension(const std::string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos || pos == 0) return "";
    return filename.substr(pos);
}

/**
 * Convert string to uppercase
 */
static std::string toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

/**
 * Check if file exists
 */
static bool fileExists(const std::string& filename) {
    return access(filename.c_str(), F_OK) == 0;
}

/**
 * Check if file is readable
 */
static bool fileReadable(const std::string& filename) {
    return access(filename.c_str(), R_OK) == 0;
}

/**
 * Check if file is executable
 */
static bool fileExecutable(const std::string& filename) {
#ifdef _WIN32
    // On Windows, X_OK doesn't work reliably - just check if file exists
    struct stat st;
    if (stat(filename.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IFREG) != 0;
#else
    return access(filename.c_str(), X_OK) == 0;
#endif
}

/**
 * Validate file extension and build command to execute
 * 
 * Parameters:
 *     language: Programming language/runtime
 *     file: Path to file to execute
 *     command: Output string for command
 *     error_msg: Output string for error message
 * 
 * Returns:
 *     true on success, false on error (error_msg will be set)
 */
static bool getCommand(const std::string& language, const std::string& file,
                      std::string& command, std::string& error_msg) {
    std::string lang_upper = toUpper(language);

    // On Windows, convert forward slashes to backslashes early for file system operations
    std::string file_path = file;
#ifdef _WIN32
    std::replace(file_path.begin(), file_path.end(), '/', '\\');
#endif

    std::string ext = getExtension(file_path);

    // Convert extension to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Extension validation - FIRST before file existence check
    bool valid_ext = false;

    if (lang_upper == "PYTHON" || lang_upper == "PY") {
        if (ext == ".py") valid_ext = true;
        else {
            error_msg = "Invalid file '" + file_path + "' for language '" + language + "'. Expected: e.g. 'file.py'";
            return false;
        }
    } else if (lang_upper == "JAVASCRIPT" || lang_upper == "JS" ||
               lang_upper == "NODE" || lang_upper == "NODEJS") {
        if (ext == ".js") valid_ext = true;
        else {
            error_msg = "Invalid file '" + file_path + "' for language '" + language + "'. Expected: e.g. 'file.js'";
            return false;
        }
    } else if (lang_upper == "RUBY" || lang_upper == "RB") {
        if (ext == ".rb") valid_ext = true;
        else {
            error_msg = "Invalid file '" + file_path + "' for language '" + language + "'. Expected: e.g. 'file.rb'";
            return false;
        }
    } else if (lang_upper == "JAR" || lang_upper == "JAVA") {
        if (ext == ".jar") valid_ext = true;
        else {
            error_msg = "Invalid file '" + file_path + "' for language '" + language + "'. Expected: e.g. 'file.jar'";
            return false;
        }
    } else if (lang_upper == "C" || lang_upper == "CPP" || lang_upper == "C++" ||
               lang_upper == "CPLUSPLUS" || lang_upper == "CS" || lang_upper == "C#" ||
               lang_upper == "CSHARP" || lang_upper == "EXE" ||
               lang_upper == "RUST" || lang_upper == "RS" ||
               lang_upper == "GO" || lang_upper == "GOLANG") {
        valid_ext = true; // Multiple valid extensions
    } else {
        error_msg = "Unsupported language: " + language;
        return false;
    }

    // File existence check
    if (!fileExists(file_path)) {
        error_msg = "File not found: " + file_path;
        return false;
    }

    // Permission checks
    if (lang_upper == "C" || lang_upper == "CPP" || lang_upper == "C++" ||
        lang_upper == "CPLUSPLUS" || lang_upper == "CS" || lang_upper == "C#" ||
        lang_upper == "CSHARP" || lang_upper == "EXE" ||
        lang_upper == "RUST" || lang_upper == "RS" ||
        lang_upper == "GO" || lang_upper == "GOLANG") {
        if (!fileExecutable(file_path)) {
            error_msg = "File is not executable: " + file_path;
            return false;
        }
    } else {
        if (!fileReadable(file_path)) {
            error_msg = "File is not readable: " + file_path;
            return false;
        }
    }

    // Auto-add ./ or .\ for compiled executables if not present and not absolute path
    std::string final_file = file_path;
    if ((lang_upper == "C" || lang_upper == "CPP" || lang_upper == "C++" ||
         lang_upper == "CPLUSPLUS" || lang_upper == "CS" || lang_upper == "C#" ||
         lang_upper == "CSHARP" || lang_upper == "EXE" ||
         lang_upper == "RUST" || lang_upper == "RS" ||
         lang_upper == "GO" || lang_upper == "GOLANG") &&
        file_path[0] != '/' && file_path[0] != '.' && !(file_path.length() > 1 && file_path[1] == ':')) {
#ifdef _WIN32
        final_file = ".\\" + file_path;
#else
        final_file = "./" + file_path;
#endif
    }
    
    // Build command
    if (lang_upper == "PYTHON" || lang_upper == "PY") {
        command = "python " + final_file;
    } else if (lang_upper == "JAVASCRIPT" || lang_upper == "JS" ||
               lang_upper == "NODE" || lang_upper == "NODEJS") {
        command = "node " + final_file;
    } else if (lang_upper == "RUBY" || lang_upper == "RB") {
        command = "ruby " + final_file;
    } else if (lang_upper == "JAR" || lang_upper == "JAVA") {
        command = "java -jar " + final_file;
    } else if (lang_upper == "GO" || lang_upper == "GOLANG") {
        if (ext == ".go") {
            command = "go run " + final_file;
        } else {
            command = final_file;
        }
    } else if (lang_upper == "CS" || lang_upper == "C#" || lang_upper == "CSHARP") {
        // C# / .NET - use dotnet for .dll, run directly for .exe
        if (ext == ".dll") {
            command = "dotnet " + final_file;
        } else {
            command = final_file;
        }
    } else {
        command = final_file;
    }
    
    return true;
}

// ============================================================================
// InputManager Implementation
// ============================================================================

/**
 * Constructor - Initialize InputManagerResponse
 */
InputManagerResponse::InputManagerResponse() 
    : request_status_set(false),
      request_status(false),
      data(""),
      optional_output(true),
      is_unique(true) {
}

/**
 * Constructor - Create a new InputManager instance
 */
InputManager::InputManager() : key("") {
}

/**
 * Destructor
 */
InputManager::~InputManager() {
}

/**
 * Send a request to another process
 * 
 * Parameters:
 *     is_unique: Expect single output (true) or multiple (false)
 *     optional_output: Output is optional (true) or required (false)
 *     data: Data to send as JSON string
 *     language: Target language/runtime
 *     file: Path to target file
 * 
 * Sets this->response with complete response details
 */
void InputManager::request(bool is_unique, bool optional_output, const std::string& data,
                          const std::string& language, const std::string& file) {
    // Generate key
    key = genKey();
    
    // Clear previous response
    response = InputManagerResponse();
    response.optional_output = optional_output;
    response.is_unique = is_unique;
    
    // Validate and get command
    std::string command;
    std::string error_msg;
    if (!getCommand(language, file, command, error_msg)) {
        response.request_status = false;
        response.request_status_set = true;
        response.errors.push_back(error_msg);
        response.warnings.push_back("Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies.");
        return;
    }
    
    // Build request JSON
    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "key", key.c_str());
    cJSON_AddBoolToObject(request, "optionalOutput", optional_output);
    cJSON_AddBoolToObject(request, "isUnique", is_unique);
    
    if (!data.empty()) {
        cJSON* parsed = cJSON_Parse(data.c_str());
        if (parsed) {
            cJSON_AddItemToObject(request, "data", parsed);
        } else {
            cJSON_AddNullToObject(request, "data");
        }
    } else {
        cJSON_AddNullToObject(request, "data");
    }
    
    char* request_str = cJSON_PrintUnformatted(request);
    std::string request_json = request_str;
    free(request_str);
    cJSON_Delete(request);
    
    // Create temporary files for input and output
    std::string temp_input, temp_output;
#ifdef _WIN32
    temp_input = "temp_input_" + std::to_string(time(nullptr)) + ".txt";
    temp_output = "temp_output_" + std::to_string(time(nullptr)) + ".txt";
#else
    temp_input = "/tmp/temp_input_" + std::to_string(time(nullptr)) + ".txt";
    temp_output = "/tmp/temp_output_" + std::to_string(time(nullptr)) + ".txt";
#endif
    
    // Write request to input file
    std::ofstream input_file(temp_input);
    if (!input_file) {
        response.request_status = false;
        response.request_status_set = true;
        response.errors.push_back("Failed to create input file");
        return;
    }
    input_file << request_json;
    input_file.close();
    
    // Build full command with input/output/error redirection
    std::string temp_error;
#ifdef _WIN32
    temp_error = "temp_error_" + std::to_string(time(nullptr)) + ".txt";
#else
    temp_error = "/tmp/temp_error_" + std::to_string(time(nullptr)) + ".txt";
#endif
    std::string full_command = command + " < " + temp_input + " > " + temp_output + " 2>" + temp_error;

    // Execute command
    int exit_code = system(full_command.c_str());

    // Remove input file
    std::remove(temp_input.c_str());

    // Check exit code
    if (exit_code != 0) {
        response.request_status = false;
        response.request_status_set = true;
        response.errors.push_back("Process exited with code " + std::to_string(exit_code));
        // Read stderr output for details
        std::ifstream err_file(temp_error);
        if (err_file) {
            std::string err_content((std::istreambuf_iterator<char>(err_file)),
                                    std::istreambuf_iterator<char>());
            err_file.close();
            if (!err_content.empty()) {
                // Trim trailing whitespace
                while (!err_content.empty() && (err_content.back() == '\n' || err_content.back() == '\r')) {
                    err_content.pop_back();
                }
                response.errors.push_back("stderr: " + err_content);
            }
        }
        response.warnings.push_back("Warning: these kind of errors result from an error in the targeted script.");
        std::remove(temp_output.c_str());
        std::remove(temp_error.c_str());
        return;
    }

    // Clean up error file on success
    std::remove(temp_error.c_str());
    
    // Read output from temporary file
    std::ifstream output_file(temp_output);
    if (!output_file) {
        response.request_status = false;
        response.request_status_set = true;
        response.errors.push_back("Failed to read output");
        std::remove(temp_output.c_str());
        return;
    }
    
    // Read and parse output
    std::vector<cJSON*> responses;
    std::string line;
    
    while (std::getline(output_file, line)) {
        if (line.empty()) continue;
        
        cJSON* json = cJSON_Parse(line.c_str());
        if (!json) continue;
        
        // Validate response has matching key or null key (for init errors)
        // This ensures we only process responses meant for this request
        cJSON* key_item = cJSON_GetObjectItem(json, "key");
        if (key_item) {
            const char* response_key = cJSON_GetStringValue(key_item);
            if (cJSON_IsNull(key_item) || (response_key && key == response_key)) {
                responses.push_back(json);
            } else {
                cJSON_Delete(json);
            }
        } else {
            cJSON_Delete(json);
        }
    }
    
    output_file.close();
    std::remove(temp_output.c_str());
    
    // Process responses
    if (!responses.empty()) {
        bool failure = false;
        
        for (cJSON* resp : responses) {
            cJSON* status = cJSON_GetObjectItem(resp, "request_status");
            if (status && cJSON_IsBool(status) && !cJSON_IsTrue(status)) {
                failure = true;
            }
            
            cJSON* errors = cJSON_GetObjectItem(resp, "errors");
            if (errors && cJSON_IsArray(errors)) {
                cJSON* error = nullptr;
                cJSON_ArrayForEach(error, errors) {
                    if (cJSON_IsString(error)) {
                        response.errors.push_back(error->valuestring);
                    }
                }
            }
        }
        
        response.request_status = !failure;
        response.request_status_set = true;
        
        // Collect data
        if (is_unique && responses.size() == 1) {
            cJSON* data = cJSON_GetObjectItem(responses[0], "data");
            if (data) {
                char* data_str = cJSON_PrintUnformatted(data);
                response.data = data_str;
                free(data_str);
            }
        } else if (is_unique && responses.size() > 1) {
            response.request_status = false;
            response.data = "";
            response.errors.push_back("Error: Expected 1 output (isUnique=True) but received " + 
                                     std::to_string(responses.size()) + ".");
        } else {
            // Multiple responses - create array
            cJSON* data_array = cJSON_CreateArray();
            for (cJSON* resp : responses) {
                cJSON* data = cJSON_GetObjectItem(resp, "data");
                if (data) {
                    cJSON_AddItemToArray(data_array, cJSON_Duplicate(data, 1));
                }
            }
            char* data_str = cJSON_PrintUnformatted(data_array);
            response.data = data_str;
            free(data_str);
            cJSON_Delete(data_array);
        }
        
        // Clean up
        for (cJSON* resp : responses) {
            cJSON_Delete(resp);
        }
    } else if (optional_output) {
        response.request_status_set = false;
        response.warnings.push_back("Warning: the output setting is set to optional, and the targeted program didn't gave any output.");
    } else {
        response.request_status = false;
        response.request_status_set = true;
        response.errors.push_back("Error: OutputManager might not be used or not correctly.");
    }
}

/**
 * Get the full response object
 *
 * Returns:
 *     The complete response with status, data, errors, warnings
 */
InputManagerResponse InputManager::getResponse() const {
    return response;
}

/**
 * Get the response data if request was successful
 *
 * Returns:
 *     Response data as JSON string, or empty string if request failed
 */
std::string InputManager::getData() const {
    if (response.request_status_set && response.request_status) {
        return response.data;
    }
    return "";
}

// ============================================================================
// OutputManager Implementation
// ============================================================================

/**
 * Initialize OutputManager and read request from stdin
 * 
 * Must be called before using output() or getData().
 * Suppresses stdout to prevent pollution of JSON protocol.
 */
void OutputManager::init() {
    // Save original stdout file descriptor so we can restore it later
    original_stdout_fd = dup(fileno(stdout));
    
    // Suppress stdout by redirecting to null device
#ifdef _WIN32
    freopen("NUL", "w", stdout);
#else
    freopen("/dev/null", "w", stdout);
#endif
    
    // Read the entire stdin (the JSON request from InputManager)
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    request_json = buffer.str();
    
    // Parse JSON
    cJSON* json = cJSON_Parse(request_json.c_str());
    if (json) {
        cJSON* key_item = cJSON_GetObjectItem(json, "key");
        if (key_item && cJSON_IsString(key_item)) key = cJSON_GetStringValue(key_item);
        
        cJSON* data_item = cJSON_GetObjectItem(json, "data");
        if (data_item) {
            char* data_str = cJSON_PrintUnformatted(data_item);
            data = data_str;
            free(data_str);
        }
        
        cJSON* opt = cJSON_GetObjectItem(json, "optionalOutput");
        if (opt) optional_output = cJSON_IsTrue(opt);
        
        cJSON* uniq = cJSON_GetObjectItem(json, "isUnique");
        if (uniq) is_unique = cJSON_IsTrue(uniq);
        
        cJSON_Delete(json);
    }
    
    // Reset state for new request
    errors.clear();
    warnings.clear();
    init_error = false;
    request_status_set = false;
    unique_state_set = false;
}

/**
 * Get the request data as JSON string
 * 
 * Returns:
 *     Request data as JSON string
 */
std::string OutputManager::getData() {
    return data;
}

/**
 * Get the request data as integer
 */
int OutputManager::getInt() {
    if (data.empty()) return 0;
    cJSON* json = cJSON_Parse(data.c_str());
    if (!json) return 0;
    double val = cJSON_GetNumberValue(json);
    cJSON_Delete(json);
    return static_cast<int>(val);
}

/**
 * Get the request data as double
 */
double OutputManager::getDouble() {
    if (data.empty()) return 0.0;
    cJSON* json = cJSON_Parse(data.c_str());
    if (!json) return 0.0;
    double val = cJSON_GetNumberValue(json);
    cJSON_Delete(json);
    return val;
}

/**
 * Get the request data as string
 */
std::string OutputManager::getString() {
    if (data.empty()) return "";
    cJSON* json = cJSON_Parse(data.c_str());
    if (!json) return "";
    if (!cJSON_IsString(json)) {
        cJSON_Delete(json);
        return "";
    }
    std::string result = json->valuestring;
    cJSON_Delete(json);
    return result;
}

/**
 * Get the request data as boolean
 */
bool OutputManager::getBool() {
    if (data.empty()) return false;
    cJSON* json = cJSON_Parse(data.c_str());
    if (!json) return false;
    bool val = cJSON_IsTrue(json);
    cJSON_Delete(json);
    return val;
}

/**
 * Helper function to restore stdout for writing response
 */
void OutputManager::restoreStdout() {
    fflush(stdout);
    dup2(original_stdout_fd, fileno(stdout));
    clearerr(stdout);
}

/**
 * Helper function to suppress stdout after writing response
 */
void OutputManager::suppressStdout() {
    fflush(stdout);
#ifdef _WIN32
    freopen("NUL", "w", stdout);
#else
    freopen("/dev/null", "w", stdout);
#endif
}

/**
 * Send response back to the calling process
 * 
 * Parameters:
 *     data: Data to send as JSON string
 * 
 * Note:
 *     Can be called multiple times if isUnique=false in request.
 *     Will error if called multiple times when isUnique=true.
 */
void OutputManager::output(const std::string& data) {
    // Check if OutputManager was initialized
    if (data.empty()) {
        if (!init_error) {
            // Restore stdout to actually write the response
            restoreStdout();
            
            request_status = false;
            errors.push_back("Error: OutputManager isn't initialized.");
            
            // Build and write JSON response
            cJSON* response = cJSON_CreateObject();
            cJSON_AddNullToObject(response, "key");
            cJSON_AddBoolToObject(response, "request_status", false);
            cJSON_AddNullToObject(response, "data");
            cJSON_AddBoolToObject(response, "optionalOutput", optional_output);
            cJSON_AddNullToObject(response, "isUnique");
            
            cJSON* errors_array = cJSON_CreateArray();
            for (const auto& err : errors) {
                cJSON_AddItemToArray(errors_array, cJSON_CreateString(err.c_str()));
            }
            cJSON_AddItemToObject(response, "errors", errors_array);
            
            cJSON* warnings_array = cJSON_CreateArray();
            cJSON_AddItemToObject(response, "warnings", warnings_array);
            
            char* response_str = cJSON_PrintUnformatted(response);
            std::cout << response_str << std::endl;
            std::cout.flush();
            free(response_str);
            cJSON_Delete(response);
            
            init_error = true;
        }
        return;
    }
    
    // Check if we can output based on isUnique setting
    // unique_state_set tracks if we've already output once
    if (!unique_state_set || !is_unique) {
        request_status = true;
        
        // Restore stdout to actually write the response
        restoreStdout();
        
        // Build and write JSON response
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "key", key.c_str());
        cJSON_AddBoolToObject(response, "request_status", true);
        
        cJSON* parsed = cJSON_Parse(data.c_str());
        if (parsed) {
            cJSON_AddItemToObject(response, "data", parsed);
        } else {
            cJSON_AddNullToObject(response, "data");
        }

        cJSON_AddBoolToObject(response, "optionalOutput", optional_output);
        cJSON_AddBoolToObject(response, "isUnique", is_unique);

        cJSON* errors_array = cJSON_CreateArray();
        cJSON_AddItemToObject(response, "errors", errors_array);

        cJSON* warnings_array = cJSON_CreateArray();
        cJSON_AddItemToObject(response, "warnings", warnings_array);

        char* response_str = cJSON_PrintUnformatted(response);
        std::cout << response_str << std::endl;
        std::cout.flush();
        free(response_str);
        cJSON_Delete(response);

    } else {
        // Multiple outputs when isUnique=true is an error
        request_status = false;
        errors.push_back("Error: outputs out of bound (isUnique: " +
                        std::to_string(unique_state) + ").");

        // Restore stdout and write error
        restoreStdout();

        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "key", key.c_str());
        cJSON_AddBoolToObject(response, "request_status", false);

        cJSON* parsed = cJSON_Parse(data.c_str());
        if (parsed) {
            cJSON_AddItemToObject(response, "data", parsed);
        } else {
            cJSON_AddNullToObject(response, "data");
        }
        
        cJSON_AddBoolToObject(response, "optionalOutput", optional_output);
        cJSON_AddBoolToObject(response, "isUnique", is_unique);
        
        cJSON* errors_array = cJSON_CreateArray();
        for (const auto& err : errors) {
            cJSON_AddItemToArray(errors_array, cJSON_CreateString(err.c_str()));
        }
        cJSON_AddItemToObject(response, "errors", errors_array);
        
        cJSON* warnings_array = cJSON_CreateArray();
        cJSON_AddItemToObject(response, "warnings", warnings_array);
        
        char* response_str = cJSON_PrintUnformatted(response);
        std::cout << response_str << std::endl;
        std::cout.flush();
        free(response_str);
        cJSON_Delete(response);
    }
    
    // Mark that we've output once
    unique_state = is_unique;
    unique_state_set = true;
    
    // Re-suppress stdout after writing response
    suppressStdout();
}

/**
 * Clean up OutputManager resources
 */
void OutputManager::cleanup() {
    if (original_stdout_fd >= 0) {
#ifdef _WIN32
        _close(original_stdout_fd);
#else
        close(original_stdout_fd);
#endif
    }
    errors.clear();
    warnings.clear();
}