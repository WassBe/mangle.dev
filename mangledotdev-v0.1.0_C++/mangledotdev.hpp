#ifndef MANGLEDOTDEV_HPP
#define MANGLEDOTDEV_HPP

#include <string>
#include <vector>
#include <sstream>
#include <cmath>

/**
 * InputManagerResponse - Contains the complete response structure
 * 
 * Fields:
 *     request_status_set: Whether request status has been set
 *     request_status: Success status of the request
 *     data: Response data as JSON string (preserves type)
 *     optional_output: Echo of request parameter
 *     is_unique: Echo of request parameter
 *     warnings: Vector of warning messages
 *     errors: Vector of error messages
 */
struct InputManagerResponse {
    bool request_status_set;
    bool request_status;
    std::string data;
    bool optional_output;
    bool is_unique;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    
    InputManagerResponse();
};

/**
 * InputManager - Manages sending requests to other processes and handling responses
 * 
 * This is an instance-based class - create one instance per request.
 * 
 * Fields:
 *     response: Complete response with status, data, errors, warnings
 * 
 * Methods:
 *     request(): Send a request to another process
 *     getResponse(): Get the response data (returns empty string on error)
 */
class InputManager {
private:
    std::string key;
    
public:
    InputManagerResponse response;
    
    /**
     * Create a new InputManager instance
     */
    InputManager();
    
    /**
     * Send a request to another process
     * 
     * @param is_unique Expect single output (true) or multiple (false)
     * @param optional_output Output is optional (true) or required (false)
     * @param data Data to send as JSON string
     * @param language Target language/runtime
     * @param file Path to target file
     * 
     * Sets this->response with fields:
     *     - request_status (bool): Success status
     *     - data (string): Response data as JSON string (preserves type)
     *     - optional_output (bool): Echo of parameter
     *     - is_unique (bool): Echo of parameter
     *     - warnings (vector<string>): Warning messages
     *     - errors (vector<string>): Error messages
     */
    void request(bool is_unique, bool optional_output, const std::string& data,
                const std::string& language, const std::string& file);
    
    /**
     * Get the full response object
     *
     * @return The complete response with status, data, errors, warnings.
     */
    InputManagerResponse getResponse() const;

    /**
     * Get the response data if request was successful
     *
     * @return The data from the response as JSON string, or empty string if request failed.
     *         Parse the JSON string to get the actual typed value.
     */
    std::string getData() const;

    /**
     * Bundle a value to JSON string for use with request()
     *
     * @param value Value to convert (int, double, string, bool)
     * @return JSON string representation
     */
    template<typename T>
    static std::string bundle(const T& value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    static std::string bundle(const char* value);

    /**
     * Destructor
     */
    ~InputManager();
};

// Template specialization for string (needs quotes)
template<>
inline std::string InputManager::bundle<std::string>(const std::string& value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    result += "\"";
    return result;
}

// Non-template overload for const char* / string literals (needs quotes)
inline std::string InputManager::bundle(const char* value) {
    return InputManager::bundle(std::string(value));
}

// Template specialization for bool
template<>
inline std::string InputManager::bundle<bool>(const bool& value) {
    return value ? "true" : "false";
}

/**
 * OutputManager - Manages receiving requests from other processes and sending responses
 * 
 * This is a static class - all methods are static.
 * Must call init() before using.
 * 
 * Static Methods:
 *     init(): Initialize and read request from stdin
 *     getData(): Get the request data as JSON string
 *     output(data): Send response back via stdout
 *     cleanup(): Clean up resources
 */
class OutputManager {
private:
    static int original_stdout_fd;
    static std::string request_json;
    static std::string key;
    static std::string data;
    static bool optional_output;
    static bool is_unique;
    static bool request_status;
    static bool request_status_set;
    static bool unique_state;
    static bool unique_state_set;
    static bool init_error;
    static std::vector<std::string> errors;
    static std::vector<std::string> warnings;
    
    static void restoreStdout();
    static void suppressStdout();
    
public:
    /**
     * Initialize OutputManager and read request from stdin
     * 
     * Must be called before using output() or getData().
     * Suppresses stdout to prevent pollution of JSON protocol.
     */
    static void init();
    
    /**
     * Get the request data as JSON string
     *
     * @return Request data as JSON string (parse to get typed value)
     */
    static std::string getData();

    /**
     * Get the request data as integer
     *
     * @return The data as int (returns whole numbers, 0 if not a number)
     */
    static int getInt();

    /**
     * Get the request data as double
     *
     * @return The data as double (0.0 if not a number)
     */
    static double getDouble();

    /**
     * Get the request data as string
     *
     * @return The data as string (empty if not a string)
     */
    static std::string getString();

    /**
     * Get the request data as boolean
     *
     * @return The data as bool (false if not a boolean)
     */
    static bool getBool();

    /**
     * Send response back to the calling process
     *
     * @param data Data to send as JSON string
     *
     * Note:
     *     Can be called multiple times if isUnique=false in request.
     *     Will error if called multiple times when isUnique=true.
     */
    static void output(const std::string& data);

    /**
     * Bundle a value to JSON string for use with output()
     *
     * @param value Value to convert (int, double, string, bool)
     * @return JSON string representation
     */
    template<typename T>
    static std::string bundle(const T& value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    static std::string bundle(const char* value);

    /**
     * Clean up OutputManager resources
     */
    static void cleanup();
};

// Template specialization for string (needs quotes)
template<>
inline std::string OutputManager::bundle<std::string>(const std::string& value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    result += "\"";
    return result;
}

// Non-template overload for const char* / string literals
inline std::string OutputManager::bundle(const char* value) {
    return OutputManager::bundle(std::string(value));
}

// Template specialization for bool
template<>
inline std::string OutputManager::bundle<bool>(const bool& value) {
    return value ? "true" : "false";
}

#endif // MANGLEDOTDEV_HPP