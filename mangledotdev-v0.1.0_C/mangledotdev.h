#ifndef MANGLEDOTDEV_H
#define MANGLEDOTDEV_H

#include <stdbool.h>

// Maximum sizes for various buffers
#define MAX_COMMAND_LENGTH 1024
#define MAX_LINE_LENGTH 4096
#define MAX_JSON_LENGTH 65536
#define MAX_ERRORS 100
#define MAX_OUTPUT_LINES 1000

/**
 * InputManagerResponse - Contains the complete response structure
 * 
 * Fields:
 *     request_status_set: Whether request status has been set
 *     request_status: Success status of the request
 *     data: Response data as JSON string (preserves type)
 *     optional_output: Echo of request parameter
 *     is_unique: Echo of request parameter
 *     warnings: Array of warning messages
 *     warning_count: Number of warnings
 *     errors: Array of error messages
 *     error_count: Number of errors
 */
typedef struct {
    bool request_status_set;
    bool request_status;
    char* data;
    bool optional_output;
    bool is_unique;
    char* warnings[MAX_ERRORS];
    int warning_count;
    char* errors[MAX_ERRORS];
    int error_count;
} InputManagerResponse;

/**
 * InputManager - Manages sending requests to other processes and handling responses
 * 
 * This is an instance-based structure - create one instance per request.
 * 
 * Fields:
 *     response: Complete response with status, data, errors, warnings
 * 
 * Functions:
 *     input_manager_create(): Create a new InputManager instance
 *     input_manager_request(): Send a request to another process
 *     input_manager_get_response(): Get the response data (returns NULL on error)
 *     input_manager_destroy(): Clean up and free resources
 */
typedef struct {
    char* key;
    InputManagerResponse* response;
} InputManager;

/**
 * OutputManager - Manages receiving requests from other processes and sending responses
 * 
 * This uses global state - all functions are module-level.
 * Must call output_manager_init() before using.
 * 
 * Functions:
 *     output_manager_init(): Initialize and read request from stdin
 *     output_manager_get_data(): Get the request data as JSON string
 *     output_manager_output(data): Send response back via stdout
 *     output_manager_cleanup(): Clean up resources
 */
typedef struct {
    int original_stdout_fd;
    char* request_json;
    char* key;
    char* data;
    bool optional_output;
    bool is_unique;
    bool request_status;
    bool request_status_set;
    bool unique_state;
    bool unique_state_set;
    bool init_error;
    char* errors[MAX_ERRORS];
    int error_count;
    char* warnings[MAX_ERRORS];
    int warning_count;
} OutputManagerData;

// InputManager functions
InputManager* input_manager_create();
void input_manager_request(InputManager* im, bool is_unique, bool optional_output,
                           const char* data, const char* language, const char* file);
InputManagerResponse* input_manager_get_response(InputManager* im);
char* input_manager_get_data(InputManager* im);
void input_manager_destroy(InputManager* im);

// InputManager bundle functions - convert values to JSON strings
char* input_manager_bundle_int(int value);
char* input_manager_bundle_double(double value);
char* input_manager_bundle_string(const char* value);
char* input_manager_bundle_bool(bool value);

// OutputManager functions
void output_manager_init();
void output_manager_output(const char* data);
void output_manager_cleanup();

// OutputManager get_data functions - get typed values directly
int output_manager_get_int();
double output_manager_get_double();
char* output_manager_get_string();
bool output_manager_get_bool();
char* output_manager_get_data();  // raw JSON string

// OutputManager bundle functions - convert values to JSON strings
char* bundle_int(int value);
char* bundle_double(double value);
char* bundle_string(const char* value);
char* bundle_bool(bool value);

#endif // MANGLEDOTDEV_H