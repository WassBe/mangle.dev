/*
Mangledotdev - Cross-language IPC Library
C Implementation

A library for inter-process communication via stdin/stdout using JSON protocol.
Supports execution of programs in: Python, JavaScript, Ruby, C, C++, C#, Java, Rust (rs), Go

Core Components:
    - InputManager: Send requests to other processes (instance-based)
    - OutputManager: Receive requests from other processes (static functions)

Protocol:
    JSON messages via stdin/stdout with unique keys for request/response matching

Dependencies:
    - cJSON library (https://github.com/DaveGamble/cJSON)

Compilation:
    gcc -o program program.c mangledotdev.c cJSON.c -I. -lm
*/

#include "mangledotdev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <math.h>

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
#define close _close
#define fileno _fileno
#define F_OK 0
#define R_OK 4
#define X_OK 1
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#include "cJSON.h"

// Global OutputManager data (singleton)
static OutputManagerData g_output_manager = {0};

/**
 * Generate a unique key for request/response matching
 *
 * Generates a 32-character hex string from 16 random bytes.
 *
 * Returns:
 *     char* - Unique key string (caller must free)
 */
static char* gen_key() {
    unsigned char bytes[16];
    char* key = (char*)malloc(33);
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
        srand((unsigned int)(time(NULL) ^ clock()));
        for (int i = 0; i < 16; i++) bytes[i] = rand() % 256;
    }
#endif
    for (int i = 0; i < 16; i++) {
        sprintf(key + i * 2, "%02x", bytes[i]);
    }
    key[32] = '\0';
    return key;
}

/**
 * Get file extension from filename
 * 
 * Parameters:
 *     filename - Path to file
 * 
 * Returns:
 *     const char* - Extension string (including dot), or empty string if no extension
 */
static const char* get_extension(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) return "";
    return dot;
}

/**
 * Check if file exists
 * 
 * Parameters:
 *     filename - Path to file
 * 
 * Returns:
 *     bool - true if file exists, false otherwise
 */
static bool file_exists(const char* filename) {
    return access(filename, F_OK) == 0;
}

/**
 * Check if file is readable
 * 
 * Parameters:
 *     filename - Path to file
 * 
 * Returns:
 *     bool - true if file is readable, false otherwise
 */
static bool file_readable(const char* filename) {
    return access(filename, R_OK) == 0;
}

/**
 * Check if file is executable
 * 
 * On Windows, X_OK doesn't work reliably, so we just check if it's a regular file.
 * 
 * Parameters:
 *     filename - Path to file
 * 
 * Returns:
 *     bool - true if file is executable, false otherwise
 */
static bool file_executable(const char* filename) {
#ifdef _WIN32
    // On Windows, the X_OK flag doesn't work reliably
    // Just check if the file exists - Windows will handle executability
    struct stat st;
    if (stat(filename, &st) != 0) return false;
    return (st.st_mode & S_IFREG) != 0;  // Check if it's a regular file
#else
    return access(filename, X_OK) == 0;
#endif
}

/**
 * Convert string to uppercase in-place
 * 
 * Parameters:
 *     str - String to convert (modified in place)
 */
static void to_upper(char* str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            str[i] = str[i] - 'a' + 'A';
        }
    }
}

/**
 * Validate file extension and build command to execute
 * 
 * Performs validation checks:
 *     1. Extension validation - checks file has correct extension for language
 *     2. File existence check - ensures file exists
 *     3. Permission check - ensures file is readable/executable as needed
 *     4. Command building - constructs the command string
 * 
 * Parameters:
 *     language - Programming language/runtime (e.g., "python", "c", "javascript")
 *     file - Path to file to execute
 *     command - Output buffer for constructed command
 *     error_msg - Output buffer for error message if validation fails
 *     error_msg_size - Size of error message buffer
 * 
 * Returns:
 *     bool - true on success, false on validation failure (error_msg will be set)
 */
static bool get_command(const char* language, const char* file, char* command,
                       char* error_msg, size_t error_msg_size) {
    char lang_upper[256];
    strncpy(lang_upper, language, sizeof(lang_upper) - 1);
    lang_upper[sizeof(lang_upper) - 1] = '\0';
    to_upper(lang_upper);

    // On Windows, convert forward slashes to backslashes early for file system operations
    char file_path[MAX_COMMAND_LENGTH];
    strncpy(file_path, file, sizeof(file_path) - 1);
    file_path[sizeof(file_path) - 1] = '\0';
#ifdef _WIN32
    for (int i = 0; file_path[i]; i++) {
        if (file_path[i] == '/') file_path[i] = '\\';
    }
#endif

    const char* ext = get_extension(file_path);
    
    // Extension validation - verify file has correct extension for the language
    bool valid_ext = false;
    
    if (strcmp(lang_upper, "PYTHON") == 0 || strcmp(lang_upper, "PY") == 0) {
        if (strcmp(ext, ".py") == 0) valid_ext = true;
        else {
            snprintf(error_msg, error_msg_size, "Invalid file '%s' for language '%s'. Expected: e.g. 'file.py'", file_path, language);
            return false;
        }
    } else if (strcmp(lang_upper, "JAVASCRIPT") == 0 || strcmp(lang_upper, "JS") == 0 ||
               strcmp(lang_upper, "NODE") == 0 || strcmp(lang_upper, "NODEJS") == 0) {
        if (strcmp(ext, ".js") == 0) valid_ext = true;
        else {
            snprintf(error_msg, error_msg_size, "Invalid file '%s' for language '%s'. Expected: e.g. 'file.js'", file_path, language);
            return false;
        }
    } else if (strcmp(lang_upper, "RUBY") == 0 || strcmp(lang_upper, "RB") == 0) {
        if (strcmp(ext, ".rb") == 0) valid_ext = true;
        else {
            snprintf(error_msg, error_msg_size, "Invalid file '%s' for language '%s'. Expected: e.g. 'file.rb'", file_path, language);
            return false;
        }
    } else if (strcmp(lang_upper, "JAVA") == 0 || strcmp(lang_upper, "JAR") == 0) {
        if (strcmp(ext, ".jar") == 0 || strcmp(ext, ".class") == 0) valid_ext = true;
        else {
            snprintf(error_msg, error_msg_size, "Invalid file '%s' for language '%s'. Expected: e.g. 'file.jar' or 'file.class'", file_path, language);
            return false;
        }
    } else if (strcmp(lang_upper, "C") == 0 || strcmp(lang_upper, "CPP") == 0 ||
               strcmp(lang_upper, "C++") == 0 || strcmp(lang_upper, "CPLUSPLUS") == 0 ||
               strcmp(lang_upper, "CS") == 0 || strcmp(lang_upper, "C#") == 0 ||
               strcmp(lang_upper, "CSHARP") == 0 || strcmp(lang_upper, "EXE") == 0 ||
               strcmp(lang_upper, "RUST") == 0 || strcmp(lang_upper, "RS") == 0) {
        valid_ext = true; // Multiple valid extensions for compiled languages
    } else {
        snprintf(error_msg, error_msg_size, "Unsupported language: %s", language);
        return false;
    }

    // File existence check
    if (!file_exists(file_path)) {
        snprintf(error_msg, error_msg_size, "File not found: %s", file_path);
        return false;
    }

    // Permission checks - compiled languages need execute permission, scripts need read
    // Java files need read permission (interpreted by JVM)
    if (strcmp(lang_upper, "C") == 0 || strcmp(lang_upper, "CPP") == 0 ||
        strcmp(lang_upper, "C++") == 0 || strcmp(lang_upper, "CPLUSPLUS") == 0 ||
        strcmp(lang_upper, "CS") == 0 || strcmp(lang_upper, "C#") == 0 ||
        strcmp(lang_upper, "CSHARP") == 0 || strcmp(lang_upper, "EXE") == 0 ||
        strcmp(lang_upper, "RUST") == 0 || strcmp(lang_upper, "RS") == 0) {
        if (!file_executable(file_path)) {
            snprintf(error_msg, error_msg_size, "File is not executable: %s", file_path);
            return false;
        }
    } else {
        if (!file_readable(file_path)) {
            snprintf(error_msg, error_msg_size, "File is not readable: %s", file_path);
            return false;
        }
    }
    
    // Build command string based on language
    // Note: file_path already has backslashes on Windows
    if (strcmp(lang_upper, "PYTHON") == 0 || strcmp(lang_upper, "PY") == 0) {
        snprintf(command, MAX_COMMAND_LENGTH, "python %s", file_path);
    } else if (strcmp(lang_upper, "JAVASCRIPT") == 0 || strcmp(lang_upper, "JS") == 0 ||
               strcmp(lang_upper, "NODE") == 0 || strcmp(lang_upper, "NODEJS") == 0) {
        snprintf(command, MAX_COMMAND_LENGTH, "node %s", file_path);
    } else if (strcmp(lang_upper, "RUBY") == 0 || strcmp(lang_upper, "RB") == 0) {
        snprintf(command, MAX_COMMAND_LENGTH, "ruby %s", file_path);
    } else if (strcmp(lang_upper, "JAVA") == 0 || strcmp(lang_upper, "JAR") == 0) {
        // Java: use -jar for .jar files, otherwise run as class
        if (strcmp(ext, ".jar") == 0) {
            snprintf(command, MAX_COMMAND_LENGTH, "java -jar %s", file_path);
        } else {
            // For .class files, need to strip extension and handle classpath
            char class_name[256];
            strncpy(class_name, file_path, sizeof(class_name) - 1);
            class_name[sizeof(class_name) - 1] = '\0';
            // Remove .class extension
            char* dot = strrchr(class_name, '.');
            if (dot) *dot = '\0';
            // Extract directory for classpath and class name
            char* last_sep = strrchr(class_name, '/');
#ifdef _WIN32
            char* last_sep_win = strrchr(class_name, '\\');
            if (last_sep_win && (!last_sep || last_sep_win > last_sep)) {
                last_sep = last_sep_win;
            }
#endif
            if (last_sep) {
                *last_sep = '\0';
                snprintf(command, MAX_COMMAND_LENGTH, "java -cp \"%s\" %s", class_name, last_sep + 1);
            } else {
                snprintf(command, MAX_COMMAND_LENGTH, "java %s", class_name);
            }
        }
    } else if (strcmp(lang_upper, "CS") == 0 || strcmp(lang_upper, "C#") == 0 ||
               strcmp(lang_upper, "CSHARP") == 0) {
        // C# / .NET - use dotnet for .dll, run directly for .exe
        if (strcmp(ext, ".dll") == 0) {
            snprintf(command, MAX_COMMAND_LENGTH, "dotnet %s", file_path);
        } else {
            // .exe can run directly
#ifdef _WIN32
            if (file_path[0] != '\\' &&
                !(strlen(file_path) > 1 && file_path[1] == ':') &&
                !(file_path[0] == '.' && file_path[1] == '\\')) {
                snprintf(command, MAX_COMMAND_LENGTH, ".\\%s", file_path);
            } else {
                snprintf(command, MAX_COMMAND_LENGTH, "%s", file_path);
            }
#else
            if (file_path[0] != '/' && file_path[0] != '.') {
                snprintf(command, MAX_COMMAND_LENGTH, "./%s", file_path);
            } else {
                snprintf(command, MAX_COMMAND_LENGTH, "%s", file_path);
            }
#endif
        }
    } else {
        // Compiled languages (C, C++, Rust, exe) - execute directly
        // Check if path needs ./ or .\ prefix (relative path without existing prefix)
#ifdef _WIN32
        if (file_path[0] != '\\' &&
            !(strlen(file_path) > 1 && file_path[1] == ':') &&
            !(file_path[0] == '.' && file_path[1] == '\\')) {
            snprintf(command, MAX_COMMAND_LENGTH, ".\\%s", file_path);
        } else {
            snprintf(command, MAX_COMMAND_LENGTH, "%s", file_path);
        }
#else
        if (file_path[0] != '/' && file_path[0] != '.') {
            snprintf(command, MAX_COMMAND_LENGTH, "./%s", file_path);
        } else {
            snprintf(command, MAX_COMMAND_LENGTH, "%s", file_path);
        }
#endif
    }
    
    return true;
}

// ============================================================================
// InputManager Implementation
// ============================================================================

/**
 * Create a new InputManager instance
 * 
 * Allocates memory for InputManager and its response structure.
 * 
 * Returns:
 *     InputManager* - Pointer to new instance, or NULL on allocation failure
 */
InputManager* input_manager_create() {
    InputManager* im = (InputManager*)malloc(sizeof(InputManager));
    if (!im) return NULL;
    
    im->key = NULL;
    im->response = (InputManagerResponse*)calloc(1, sizeof(InputManagerResponse));
    if (!im->response) {
        free(im);
        return NULL;
    }
    
    return im;
}

/**
 * Send a request to another process
 * 
 * This function:
 *     1. Generates a unique key for request/response matching
 *     2. Validates the target file and builds the command
 *     3. Creates a JSON request with the provided data
 *     4. Writes request to temporary file
 *     5. Executes the target process with stdin/stdout redirection
 *     6. Reads and parses the JSON response
 *     7. Validates response key matches request key
 *     8. Populates im->response with results
 * 
 * Parameters:
 *     im - InputManager instance
 *     is_unique - true to expect single output, false for multiple
 *     optional_output - true if output is optional, false if required
 *     data - Data to send as JSON string
 *     language - Target language/runtime
 *     file - Path to target file
 * 
 * Sets im->response fields:
 *     request_status_set - Whether status is set
 *     request_status - true on success, false on failure
 *     data - Response data as JSON string (preserves type)
 *     optional_output - Echo of parameter
 *     is_unique - Echo of parameter
 *     warnings - Array of warning messages
 *     errors - Array of error messages
 */
void input_manager_request(InputManager* im, bool is_unique, bool optional_output,
                           const char* data, const char* language, const char* file) {
    if (!im || !im->response) return;
    
    // Generate unique key for this request
    if (im->key) free(im->key);
    im->key = gen_key();
    
    // Clean up previous response data
    if (im->response->data) {
        free(im->response->data);
        im->response->data = NULL;
    }
    for (int i = 0; i < im->response->error_count; i++) {
        free(im->response->errors[i]);
    }
    for (int i = 0; i < im->response->warning_count; i++) {
        free(im->response->warnings[i]);
    }
    im->response->error_count = 0;
    im->response->warning_count = 0;
    
    // Initialize response structure
    im->response->request_status_set = false;
    im->response->optional_output = optional_output;
    im->response->is_unique = is_unique;
    
    // Validate file and build command
    char command[MAX_COMMAND_LENGTH];
    char error_msg[1024];
    if (!get_command(language, file, command, error_msg, sizeof(error_msg))) {
        im->response->request_status = false;
        im->response->request_status_set = true;
        im->response->errors[im->response->error_count++] = strdup(error_msg);
        im->response->warnings[im->response->warning_count++] = 
            strdup("Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies.");
        return;
    }
    
    // Build request JSON with key, options, and data
    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "key", im->key);
    cJSON_AddBoolToObject(request, "optionalOutput", optional_output);
    cJSON_AddBoolToObject(request, "isUnique", is_unique);
    
    if (data && strlen(data) > 0) {
        cJSON* parsed = cJSON_Parse(data);
        if (parsed) {
            cJSON_AddItemToObject(request, "data", parsed);
        } else {
            cJSON_AddNullToObject(request, "data");
        }
    } else {
        cJSON_AddNullToObject(request, "data");
    }
    
    char* request_str = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    
    // Create temporary files for IPC - input for request, output for response
    char temp_input[256];
    char temp_output[256];
#ifdef _WIN32
    snprintf(temp_input, sizeof(temp_input), "temp_input_%d.txt", (int)time(NULL));
    snprintf(temp_output, sizeof(temp_output), "temp_output_%d.txt", (int)time(NULL));
#else
    snprintf(temp_input, sizeof(temp_input), "/tmp/temp_input_%d.txt", (int)time(NULL));
    snprintf(temp_output, sizeof(temp_output), "/tmp/temp_output_%d.txt", (int)time(NULL));
#endif
    
    // Write request JSON to input file
    FILE* input_file = fopen(temp_input, "w");
    if (!input_file) {
        im->response->request_status = false;
        im->response->request_status_set = true;
        im->response->errors[im->response->error_count++] = strdup("Failed to create input file");
        free(request_str);
        return;
    }
    fprintf(input_file, "%s", request_str);
    fclose(input_file);
    free(request_str);
    
    // Build full command with stdin/stdout redirection
    char full_command[MAX_COMMAND_LENGTH * 2];
#ifdef _WIN32
    snprintf(full_command, sizeof(full_command), "%s < %s > %s 2>&1", 
             command, temp_input, temp_output);
#else
    snprintf(full_command, sizeof(full_command), "%s < %s > %s 2>&1", 
             command, temp_input, temp_output);
#endif
    
    // Execute the command
    int exit_code = system(full_command);
    
    // Clean up input file
    remove(temp_input);
    
    // Check if process executed successfully
    if (exit_code != 0) {
        im->response->request_status = false;
        im->response->request_status_set = true;
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Process exited with code %d", exit_code);
        im->response->errors[im->response->error_count++] = strdup(err_msg);
        im->response->warnings[im->response->warning_count++] = 
            strdup("Warning: these kind of errors result from an error in the targeted script.");
        remove(temp_output);
        return;
    }
    
    // Read response from output file
    FILE* output_file = fopen(temp_output, "r");
    if (!output_file) {
        im->response->request_status = false;
        im->response->request_status_set = true;
        im->response->errors[im->response->error_count++] = strdup("Failed to read output");
        remove(temp_output);
        return;
    }
    
    // Parse output line by line - each line should be a JSON response
    char line[MAX_LINE_LENGTH];
    cJSON* responses[MAX_OUTPUT_LINES];
    int response_count = 0;
    
    while (fgets(line, sizeof(line), output_file) && response_count < MAX_OUTPUT_LINES) {
        // Remove newline and carriage return characters
        line[strcspn(line, "\n")] = 0;
        line[strcspn(line, "\r")] = 0;
        if (strlen(line) == 0) continue;
        
        cJSON* json = cJSON_Parse(line);
        if (!json) continue;
        
        // Validate response has matching key or null key (for init errors)
        // This ensures we only process responses meant for this request
        cJSON* key_item = cJSON_GetObjectItem(json, "key");
        if (key_item) {
            const char* response_key = cJSON_GetStringValue(key_item);
            if (cJSON_IsNull(key_item) || (response_key && strcmp(response_key, im->key) == 0)) {
                responses[response_count++] = json;
            } else {
                cJSON_Delete(json);
            }
        } else {
            cJSON_Delete(json);
        }
    }
    
    fclose(output_file);
    remove(temp_output);
    
    // Process collected responses
    if (response_count > 0) {
        bool failure = false;
        
        // Check if any response indicates failure
        for (int i = 0; i < response_count; i++) {
            cJSON* status = cJSON_GetObjectItem(responses[i], "request_status");
            if (status && cJSON_IsBool(status) && !cJSON_IsTrue(status)) {
                failure = true;
            }
            
            // Collect all error messages
            cJSON* errors = cJSON_GetObjectItem(responses[i], "errors");
            if (errors && cJSON_IsArray(errors)) {
                cJSON* error = NULL;
                cJSON_ArrayForEach(error, errors) {
                    if (cJSON_IsString(error) && im->response->error_count < MAX_ERRORS) {
                        im->response->errors[im->response->error_count++] = strdup(error->valuestring);
                    }
                }
            }
        }
        
        im->response->request_status = !failure;
        im->response->request_status_set = true;
        
        // Collect data from responses
        if (is_unique && response_count == 1) {
            // Single response expected and received
            cJSON* data = cJSON_GetObjectItem(responses[0], "data");
            if (data) {
                im->response->data = cJSON_PrintUnformatted(data);
            }
        } else if (is_unique && response_count > 1) {
            // Multiple responses when only one expected - error
            im->response->request_status = false;
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Error: Expected 1 output (isUnique=True) but received %d.", response_count);
            im->response->errors[im->response->error_count++] = strdup(err_msg);
            im->response->data = NULL;
        } else {
            // Multiple responses expected - collect into array
            cJSON* data_array = cJSON_CreateArray();
            for (int i = 0; i < response_count; i++) {
                cJSON* data = cJSON_GetObjectItem(responses[i], "data");
                if (data) {
                    cJSON_AddItemToArray(data_array, cJSON_Duplicate(data, 1));
                }
            }
            im->response->data = cJSON_PrintUnformatted(data_array);
            cJSON_Delete(data_array);
        }
        
        // Clean up response JSON objects
        for (int i = 0; i < response_count; i++) {
            cJSON_Delete(responses[i]);
        }
    } else if (optional_output) {
        // No output received but it's optional - not an error
        im->response->request_status_set = false;
        im->response->warnings[im->response->warning_count++] = 
            strdup("Warning: the output setting is set to optional, and the targeted program didn't gave any output.");
    } else {
        // No output received and it's required - error
        im->response->request_status = false;
        im->response->request_status_set = true;
        im->response->errors[im->response->error_count++] = 
            strdup("Error: OutputManager might not be used or not correctly.");
    }
}

/**
 * Get the full response object
 *
 * Parameters:
 *     im - InputManager instance
 *
 * Returns:
 *     InputManagerResponse* - The complete response with status, data, errors, warnings
 */
InputManagerResponse* input_manager_get_response(InputManager* im) {
    if (!im) return NULL;
    return im->response;
}

/**
 * Get the response data if request was successful
 *
 * Parameters:
 *     im - InputManager instance
 *
 * Returns:
 *     char* - Response data as JSON string, or NULL if request failed
 *            The returned pointer is owned by im->response and should not be freed
 */
char* input_manager_get_data(InputManager* im) {
    if (!im || !im->response) return NULL;
    if (im->response->request_status_set && im->response->request_status) {
        return im->response->data;
    }
    return NULL;
}

/**
 * Clean up and free InputManager resources
 * 
 * Frees all allocated memory including response data, errors, and warnings.
 * 
 * Parameters:
 *     im - InputManager instance to destroy
 */
void input_manager_destroy(InputManager* im) {
    if (!im) return;

    if (im->key) free(im->key);

    if (im->response) {
        if (im->response->data) free(im->response->data);
        for (int i = 0; i < im->response->error_count; i++) {
            free(im->response->errors[i]);
        }
        for (int i = 0; i < im->response->warning_count; i++) {
            free(im->response->warnings[i]);
        }
        free(im->response);
    }

    free(im);
}

/**
 * Bundle an integer value to JSON string
 *
 * Parameters:
 *     value - Integer to convert
 *
 * Returns:
 *     char* - JSON string (caller must free)
 */
char* input_manager_bundle_int(int value) {
    char* result = (char*)malloc(32);
    snprintf(result, 32, "%d", value);
    return result;
}

/**
 * Bundle a double value to JSON string
 *
 * Parameters:
 *     value - Double to convert
 *
 * Returns:
 *     char* - JSON string (caller must free)
 */
char* input_manager_bundle_double(double value) {
    char* result = (char*)malloc(64);
    snprintf(result, 64, "%g", value);
    return result;
}

/**
 * Bundle a string value to JSON string
 *
 * Parameters:
 *     value - String to convert
 *
 * Returns:
 *     char* - JSON string with quotes (caller must free)
 */
char* input_manager_bundle_string(const char* value) {
    cJSON* json = cJSON_CreateString(value);
    char* result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return result;
}

/**
 * Bundle a boolean value to JSON string
 *
 * Parameters:
 *     value - Boolean to convert
 *
 * Returns:
 *     char* - JSON string (caller must free)
 */
char* input_manager_bundle_bool(bool value) {
    char* result = strdup(value ? "true" : "false");
    return result;
}

// ============================================================================
// OutputManager Implementation
// ============================================================================

/**
 * Initialize OutputManager and read request from stdin
 * 
 * This function:
 *     1. Saves the original stdout file descriptor using dup()
 *     2. Redirects stdout to null device to suppress output
 *     3. Reads the entire JSON request from stdin
 *     4. Parses the request to extract key, data, and options
 *     5. Resets state for the new request
 * 
 * Must be called before using output_manager_output() or output_manager_get_data().
 */
void output_manager_init() {
    // Save original stdout file descriptor so we can restore it later
    g_output_manager.original_stdout_fd = dup(fileno(stdout));
    
    // Suppress stdout by redirecting to null device
    // This prevents printf/print statements from polluting the JSON response
#ifdef _WIN32
    freopen("NUL", "w", stdout);
#else
    freopen("/dev/null", "w", stdout);
#endif
    
    // Read the entire stdin (the JSON request from InputManager)
    char buffer[MAX_JSON_LENGTH];
    size_t total_read = 0;
    
    while (total_read < sizeof(buffer) - 1) {
        int c = fgetc(stdin);
        if (c == EOF) break;
        buffer[total_read++] = (char)c;
    }
    buffer[total_read] = '\0';
    
    g_output_manager.request_json = strdup(buffer);
    
    // Parse the JSON request
    cJSON* json = cJSON_Parse(g_output_manager.request_json);
    if (json) {
        cJSON* key = cJSON_GetObjectItem(json, "key");
        if (key && cJSON_IsString(key)) g_output_manager.key = strdup(cJSON_GetStringValue(key));
        
        cJSON* data = cJSON_GetObjectItem(json, "data");
        if (data) g_output_manager.data = cJSON_PrintUnformatted(data);
        
        cJSON* opt = cJSON_GetObjectItem(json, "optionalOutput");
        if (opt) g_output_manager.optional_output = cJSON_IsTrue(opt);
        
        cJSON* uniq = cJSON_GetObjectItem(json, "isUnique");
        if (uniq) g_output_manager.is_unique = cJSON_IsTrue(uniq);
        
        cJSON_Delete(json);
    }
    
    // Reset state for new request
    for (int i = 0; i < g_output_manager.error_count; i++) {
        free(g_output_manager.errors[i]);
    }
    for (int i = 0; i < g_output_manager.warning_count; i++) {
        free(g_output_manager.warnings[i]);
    }
    g_output_manager.error_count = 0;
    g_output_manager.warning_count = 0;
    g_output_manager.init_error = false;
    g_output_manager.request_status_set = false;
    g_output_manager.unique_state_set = false;
}

/**
 * Get the request data as JSON string
 *
 * Returns:
 *     char* - Request data as JSON string (parse to get typed value)
 *            The returned pointer is owned by g_output_manager and should not be freed
 */
char* output_manager_get_data() {
    return g_output_manager.data;
}

/**
 * Get the request data as integer
 *
 * Returns:
 *     int - The data as integer (0 if not a number)
 */
int output_manager_get_int() {
    if (!g_output_manager.data) return 0;
    cJSON* json = cJSON_Parse(g_output_manager.data);
    if (!json) return 0;
    double val = cJSON_GetNumberValue(json);
    cJSON_Delete(json);
    return (int)val;
}

/**
 * Get the request data as double
 *
 * Returns:
 *     double - The data as double (0.0 if not a number)
 */
double output_manager_get_double() {
    if (!g_output_manager.data) return 0.0;
    cJSON* json = cJSON_Parse(g_output_manager.data);
    if (!json) return 0.0;
    double val = cJSON_GetNumberValue(json);
    cJSON_Delete(json);
    return val;
}

/**
 * Get the request data as string
 *
 * Returns:
 *     char* - The data as string (caller must free), or NULL if not a string
 */
char* output_manager_get_string() {
    if (!g_output_manager.data) return NULL;
    cJSON* json = cJSON_Parse(g_output_manager.data);
    if (!json) return NULL;
    if (!cJSON_IsString(json)) {
        cJSON_Delete(json);
        return NULL;
    }
    char* result = strdup(json->valuestring);
    cJSON_Delete(json);
    return result;
}

/**
 * Get the request data as boolean
 *
 * Returns:
 *     bool - The data as boolean (false if not a boolean)
 */
bool output_manager_get_bool() {
    if (!g_output_manager.data) return false;
    cJSON* json = cJSON_Parse(g_output_manager.data);
    if (!json) return false;
    bool val = cJSON_IsTrue(json);
    cJSON_Delete(json);
    return val;
}

/**
 * Bundle an integer value to JSON string (OutputManager version)
 *
 * Parameters:
 *     value - Integer to convert
 *
 * Returns:
 *     char* - JSON string (caller must free)
 */
char* bundle_int(int value) {
    char* result = (char*)malloc(32);
    snprintf(result, 32, "%d", value);
    return result;
}

/**
 * Bundle a double value to JSON string (OutputManager version)
 *
 * Parameters:
 *     value - Double to convert
 *
 * Returns:
 *     char* - JSON string (caller must free)
 */
char* bundle_double(double value) {
    char* result = (char*)malloc(64);
    snprintf(result, 64, "%g", value);
    return result;
}

/**
 * Bundle a string value to JSON string (OutputManager version)
 *
 * Parameters:
 *     value - String to convert
 *
 * Returns:
 *     char* - JSON string with quotes (caller must free)
 */
char* bundle_string(const char* value) {
    cJSON* json = cJSON_CreateString(value);
    char* result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return result;
}

/**
 * Bundle a boolean value to JSON string (OutputManager version)
 *
 * Parameters:
 *     value - Boolean to convert
 *
 * Returns:
 *     char* - JSON string (caller must free)
 */
char* bundle_bool(bool value) {
    char* result = strdup(value ? "true" : "false");
    return result;
}

/**
 * Restore stdout using dup2()
 * 
 * Restores the original stdout file descriptor so we can write the JSON response.
 */
static void restore_stdout() {
    fflush(stdout);
    // Restore original stdout using dup2()
    dup2(g_output_manager.original_stdout_fd, fileno(stdout));
    clearerr(stdout);
}

/**
 * Suppress stdout by redirecting to null device
 * 
 * Re-suppresses stdout after writing response to prevent further output pollution.
 */
static void suppress_stdout() {
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
 * This function:
 *     1. Checks if OutputManager was initialized
 *     2. Validates output uniqueness constraints
 *     3. Restores stdout temporarily
 *     4. Builds JSON response with key, status, data, and metadata
 *     5. Writes response to stdout
 *     6. Re-suppresses stdout
 * 
 * Parameters:
 *     data - Data to send as JSON string
 * 
 * Note:
 *     Can be called multiple times if isUnique=false in request.
 *     Will error if called multiple times when isUnique=true.
 */
void output_manager_output(const char* data) {
    // Check if OutputManager was initialized
    if (!g_output_manager.data) {
        if (!g_output_manager.init_error) {
            // Restore stdout to write error response
            restore_stdout();
            
            g_output_manager.request_status = false;
            g_output_manager.errors[g_output_manager.error_count++] = 
                strdup("Error: OutputManager isn't initialized.");
            
            // Build and write error response
            cJSON* response = cJSON_CreateObject();
            cJSON_AddNullToObject(response, "key");
            cJSON_AddBoolToObject(response, "request_status", false);
            cJSON_AddNullToObject(response, "data");
            cJSON_AddBoolToObject(response, "optionalOutput", g_output_manager.optional_output);
            cJSON_AddNullToObject(response, "isUnique");
            
            cJSON* errors = cJSON_CreateArray();
            for (int i = 0; i < g_output_manager.error_count; i++) {
                cJSON_AddItemToArray(errors, cJSON_CreateString(g_output_manager.errors[i]));
            }
            cJSON_AddItemToObject(response, "errors", errors);
            
            cJSON* warnings = cJSON_CreateArray();
            cJSON_AddItemToObject(response, "warnings", warnings);
            
            char* response_str = cJSON_PrintUnformatted(response);
            printf("%s\n", response_str);
            fflush(stdout);
            free(response_str);
            cJSON_Delete(response);
            
            // Mark that we've sent init error
            g_output_manager.init_error = true;
        }
        return;
    }
    
    // Check if we can output based on isUnique setting
    // unique_state_set tracks if we've already output once
    if (!g_output_manager.unique_state_set || !g_output_manager.is_unique) {
        // First output or multiple outputs allowed
        g_output_manager.request_status = true;
        
        // Restore stdout to write response
        restore_stdout();
        
        // Build success response
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "key", g_output_manager.key);
        cJSON_AddBoolToObject(response, "request_status", true);
        
        cJSON* parsed = cJSON_Parse(data);
        if (parsed) {
            cJSON_AddItemToObject(response, "data", parsed);
        } else {
            cJSON_AddNullToObject(response, "data");
        }

        cJSON_AddBoolToObject(response, "optionalOutput", g_output_manager.optional_output);
        cJSON_AddBoolToObject(response, "isUnique", g_output_manager.is_unique);

        cJSON* errors = cJSON_CreateArray();
        cJSON_AddItemToObject(response, "errors", errors);

        cJSON* warnings = cJSON_CreateArray();
        cJSON_AddItemToObject(response, "warnings", warnings);

        char* response_str = cJSON_PrintUnformatted(response);
        printf("%s\n", response_str);
        fflush(stdout);
        free(response_str);
        cJSON_Delete(response);

    } else {
        // Multiple outputs when isUnique=true is an error
        g_output_manager.request_status = false;
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Error: outputs out of bound (isUnique: %d).", g_output_manager.unique_state);
        g_output_manager.errors[g_output_manager.error_count++] = strdup(err_msg);

        // Restore stdout and write error
        restore_stdout();

        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "key", g_output_manager.key);
        cJSON_AddBoolToObject(response, "request_status", false);

        cJSON* parsed = cJSON_Parse(data);
        if (parsed) {
            cJSON_AddItemToObject(response, "data", parsed);
        } else {
            cJSON_AddNullToObject(response, "data");
        }
        
        cJSON_AddBoolToObject(response, "optionalOutput", g_output_manager.optional_output);
        cJSON_AddBoolToObject(response, "isUnique", g_output_manager.is_unique);
        
        cJSON* errors = cJSON_CreateArray();
        for (int i = 0; i < g_output_manager.error_count; i++) {
            cJSON_AddItemToArray(errors, cJSON_CreateString(g_output_manager.errors[i]));
        }
        cJSON_AddItemToObject(response, "errors", errors);
        
        cJSON* warnings = cJSON_CreateArray();
        cJSON_AddItemToObject(response, "warnings", warnings);
        
        char* response_str = cJSON_PrintUnformatted(response);
        printf("%s\n", response_str);
        fflush(stdout);
        free(response_str);
        cJSON_Delete(response);
    }
    
    // Mark that we've output once - tracks uniqueness state
    g_output_manager.unique_state = g_output_manager.is_unique;
    g_output_manager.unique_state_set = true;
    
    // Re-suppress stdout after writing response
    suppress_stdout();
}

/**
 * Clean up OutputManager resources
 * 
 * Frees all allocated memory and closes file descriptors.
 */
void output_manager_cleanup() {
    if (g_output_manager.original_stdout_fd >= 0) {
        close(g_output_manager.original_stdout_fd);
    }
    if (g_output_manager.request_json) free(g_output_manager.request_json);
    if (g_output_manager.key) free(g_output_manager.key);
    if (g_output_manager.data) free(g_output_manager.data);
    for (int i = 0; i < g_output_manager.error_count; i++) {
        free(g_output_manager.errors[i]);
    }
    for (int i = 0; i < g_output_manager.warning_count; i++) {
        free(g_output_manager.warnings[i]);
    }
}