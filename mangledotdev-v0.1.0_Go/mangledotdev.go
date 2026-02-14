package main

import (
	"bufio"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

// InputManagerResponse represents the response structure
type InputManagerResponse struct {
	RequestStatusSet bool     `json:"request_status_set"`
	RequestStatus    bool     `json:"request_status"`
	Data             string   `json:"data"` // JSON string to preserve types
	OptionalOutput   bool     `json:"optionalOutput"`
	IsUnique         bool     `json:"isUnique"`
	Warnings         []string `json:"warnings"`
	Errors           []string `json:"errors"`
}

// InputManager handles sending requests to other processes
//
// This is an instance-based struct - create one instance per request.
//
// Fields:
//
//	Response: Complete response with status, data, errors, warnings
//
// Methods:
//
//	Request(): Send a request to another process
//	GetResponse(): Get the response data (returns empty string on error)
type InputManager struct {
	key         string
	rawRequest  map[string]interface{}
	request     string
	responseObj []map[string]interface{}
	Response    InputManagerResponse
}

// NewInputManager creates a new InputManager instance
func NewInputManager() *InputManager {
	return &InputManager{
		key:         "",
		rawRequest:  make(map[string]interface{}),
		request:     "",
		responseObj: []map[string]interface{}{},
		Response: InputManagerResponse{
			RequestStatusSet: false,
			RequestStatus:    false,
			Data:             "",
			OptionalOutput:   true,
			IsUnique:         true,
			Warnings:         []string{},
			Errors:           []string{},
		},
	}
}

// Bundle converts any data to a JSON string for use with Request()
//
// Parameters:
//
//	data: Any JSON-serializable value (string, int, float, bool, slice, map, struct)
//
// Returns:
//
//	string: JSON string representation of the data
func (im *InputManager) Bundle(data interface{}) string {
	jsonBytes, _ := json.Marshal(data)
	return string(jsonBytes)
}

// Generate a unique key for request/response matching
func genKey() string {
	b := make([]byte, 16)
	rand.Read(b)
	return hex.EncodeToString(b)
}

// Validate file and build command to execute
//
// Parameters:
//
//	language: Programming language/runtime
//	file: Path to file to execute
//
// Returns:
//
//	[]string: Command array for subprocess
//	error: Invalid file extension, file not found, or permission error
func (im *InputManager) getCommand(language, file string) ([]string, error) {
	langUpper := strings.ToUpper(language)
	fileExt := strings.ToLower(filepath.Ext(file))

	// Extension validation - FIRST before file existence check
	extensionMap := map[string][]string{
		"PYTHON":     {".py"},
		"PY":         {".py"},
		"JAVASCRIPT": {".js"},
		"JS":         {".js"},
		"NODE":       {".js"},
		"NODEJS":     {".js"},
		"RUBY":       {".rb"},
		"RB":         {".rb"},
		"C":          {".c", ".out", ".exe", ""},
		"CS":         {".exe", ".dll", ""},
		"CPP":        {".cpp", ".cc", ".cxx", ".out", ".exe", ""},
		"C#":         {".exe", ".dll", ""},
		"C++":        {".cpp", ".cc", ".cxx", ".out", ".exe", ""},
		"CSHARP":     {".exe", ".dll", ""},
		"CPLUSPLUS":  {".cpp", ".cc", ".cxx", ".out", ".exe", ""},
		"EXE":        {".cpp", ".cc", ".cxx", ".out", ".exe", ""},
		"JAR":        {".jar"},
		"JAVA":       {".jar"},
		"RUST":       {".rs", ".exe", ".out", ""},
		"RS":         {".rs", ".exe", ".out", ""},
		"GO":         {".go", ".exe", ".out", ""},
		"GOLANG":     {".go", ".exe", ".out", ""},
	}

	if validExts, ok := extensionMap[langUpper]; ok {
		found := false
		for _, ext := range validExts {
			if fileExt == ext {
				found = true
				break
			}
		}
		if !found {
			expected := strings.Join(validExts, ", ")
			return nil, fmt.Errorf("Invalid file '%s' for language '%s'. Expected: e.g. 'file%s'", file, language, expected)
		}
	}

	// File existence check
	if _, err := os.Stat(file); os.IsNotExist(err) {
		return nil, fmt.Errorf("File not found: %s", file)
	}

	info, err := os.Stat(file)
	if err != nil {
		return nil, err
	}
	if info.IsDir() {
		return nil, fmt.Errorf("Path is not a file: %s", file)
	}

	// Permission checks
	compiledLangs := []string{"C", "CS", "CPP", "C#", "C++", "CSHARP", "CPLUSPLUS", "EXE", "RUST", "RS", "GO", "GOLANG"}
	isCompiled := false
	for _, lang := range compiledLangs {
		if lang == langUpper {
			isCompiled = true
			break
		}
	}

	// On Windows, skip executable check as it's unreliable
	if isCompiled && runtime.GOOS != "windows" {
		if info.Mode()&0111 == 0 {
			return nil, fmt.Errorf("File is not executable: %s", file)
		}
	}

	// Auto-add ./ for compiled executables if not present and not absolute path
	if isCompiled && !filepath.IsAbs(file) && !strings.HasPrefix(file, "./") && !strings.HasPrefix(file, ".\\") {
		file = "./" + file
	}

	// Build command
	langMap := map[string][]string{
		"PYTHON":     {"python", file},
		"PY":         {"python", file},
		"JAVASCRIPT": {"node", file},
		"JS":         {"node", file},
		"NODE":       {"node", file},
		"NODEJS":     {"node", file},
		"RUBY":       {"ruby", file},
		"RB":         {"ruby", file},
		"C":          {file},
		"CS":         {file},
		"CPP":        {file},
		"C#":         {file},
		"C++":        {file},
		"CSHARP":     {file},
		"CPLUSPLUS":  {file},
		"EXE":        {file},
		"JAR":        {"java", "-jar", file},
		"JAVA":       {"java", "-jar", file},
		"RUST":       {file},
		"RS":         {file},
		"GOLANG":     {"go", "run", file},
	}

	if fileExt == ".go" {
		langMap["GO"] = []string{"go", "run", file}
	} else {
		langMap["GO"] = []string{file}
	}

	if cmd, ok := langMap[langUpper]; ok {
		return cmd, nil
	}

	return nil, fmt.Errorf("Unsupported language: %s", language)
}

// Request sends a request to another process
//
// Parameters:
//
//	isUnique: Expect single output (true) or multiple (false)
//	optionalOutput: Output is optional (true) or required (false)
//	data: Data to send as JSON string (any JSON-serializable type)
//	language: Target language/runtime
//	file: Path to target file
//
// Sets im.Response with fields:
//   - RequestStatus (bool): Success status
//   - Data (string): Response data as JSON string (preserves type)
//   - OptionalOutput (bool): Echo of parameter
//   - IsUnique (bool): Echo of parameter
//   - Warnings ([]string): Warning messages
//   - Errors ([]string): Error messages
func (im *InputManager) Request(isUnique, optionalOutput bool, data, language, file string) {
	defer func() {
		if r := recover(); r != nil {
			im.Response.RequestStatus = false
			im.Response.RequestStatusSet = true
			im.Response.Warnings = []string{"Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies."}
			im.Response.Errors = []string{fmt.Sprintf("Error: %v", r)}
		}
	}()

	im.key = genKey()
	command, err := im.getCommand(language, file)
	if err != nil {
		im.Response.RequestStatus = false
		im.Response.RequestStatusSet = true
		im.Response.OptionalOutput = optionalOutput
		im.Response.IsUnique = isUnique
		im.Response.Warnings = []string{"Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies."}
		im.Response.Errors = []string{fmt.Sprintf("Error: %s", err.Error())}
		return
	}

	im.Response = InputManagerResponse{
		OptionalOutput: optionalOutput,
		IsUnique:       isUnique,
		Warnings:       []string{},
		Errors:         []string{},
	}

	requestMap := map[string]interface{}{
		"key":            im.key,
		"optionalOutput": optionalOutput,
		"isUnique":       isUnique,
		"data":           nil,
	}

	if data != "" {
		var parsed interface{}
		if err := json.Unmarshal([]byte(data), &parsed); err == nil {
			requestMap["data"] = parsed
		}
	}

	requestBytes, _ := json.Marshal(requestMap)
	im.request = string(requestBytes)

	cmd := exec.Command(command[0], command[1:]...)
	stdin, _ := cmd.StdinPipe()
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()

	if err := cmd.Start(); err != nil {
		im.Response.RequestStatus = false
		im.Response.RequestStatusSet = true
		im.Response.Errors = append(im.Response.Errors, fmt.Sprintf("Failed to start process: %s", err.Error()))
		return
	}

	io.WriteString(stdin, im.request)
	stdin.Close()

	outputBytes, _ := io.ReadAll(stdout)
	stderrBytes, _ := io.ReadAll(stderr)

	cmd.Wait()

	exitCode := cmd.ProcessState.ExitCode()
	if exitCode != 0 {
		im.Response.RequestStatus = false
		im.Response.RequestStatusSet = true
		im.Response.Errors = append(im.Response.Errors, fmt.Sprintf("Process exited with code %d", exitCode))
		if len(stderrBytes) > 0 {
			im.Response.Errors = append(im.Response.Errors, fmt.Sprintf("stderr: %s", string(stderrBytes)))
		}
		im.Response.Warnings = append(im.Response.Warnings, "Warning: these kind of errors result from an error in the targeted script.")
		return
	}

	output := string(outputBytes)
	lines := strings.Split(strings.TrimSpace(output), "\n")

	im.responseObj = []map[string]interface{}{}
	for _, line := range lines {
		if strings.TrimSpace(line) == "" {
			continue
		}

		var jsonData map[string]interface{}
		if err := json.Unmarshal([]byte(line), &jsonData); err != nil {
			// Ignore lines that aren't valid JSON (e.g., debug prints)
			continue
		}

		// Validate response has matching key or null key (for init errors)
		// This ensures we only process responses meant for this request
		if keyVal, ok := jsonData["key"]; ok {
			if keyVal == nil || keyVal == im.key {
				im.responseObj = append(im.responseObj, jsonData)
			}
		}
	}

	if len(im.responseObj) > 0 {
		failure := false
		for _, resp := range im.responseObj {
			if status, ok := resp["request_status"].(bool); ok && !status {
				failure = true
			}

			if errors, ok := resp["errors"].([]interface{}); ok {
				for _, err := range errors {
					if errStr, ok := err.(string); ok {
						im.Response.Errors = append(im.Response.Errors, errStr)
					}
				}
			}
		}

		im.Response.RequestStatus = !failure
		im.Response.RequestStatusSet = true
		im.Response.IsUnique = im.responseObj[0]["isUnique"].(bool)

		dataList := []interface{}{}
		for _, resp := range im.responseObj {
			dataList = append(dataList, resp["data"])
		}

		if im.Response.IsUnique {
			if len(dataList) == 1 {
				// Store as JSON string to preserve type
				dataBytes, _ := json.Marshal(dataList[0])
				im.Response.Data = string(dataBytes)
			} else {
				im.Response.RequestStatus = false
				im.Response.Data = ""
				im.Response.Errors = append(im.Response.Errors, fmt.Sprintf("Error: Expected 1 output (isUnique=True) but received %d.", len(dataList)))
			}
		} else {
			dataBytes, _ := json.Marshal(dataList)
			im.Response.Data = string(dataBytes)
		}
	} else if optionalOutput {
		im.Response.RequestStatusSet = false
		im.Response.Warnings = append(im.Response.Warnings, "Warning: the output setting is set to optional, and the targeted program didn't gave any output.")
	} else {
		im.Response.RequestStatus = false
		im.Response.RequestStatusSet = true
		im.Response.Errors = append(im.Response.Errors, "Error: OutputManager might not be used or not correctly.")
	}
}

// GetResponse returns the full response object
//
// Returns:
//
//	InputManagerResponse: The complete response with status, data, errors, warnings.
func (im *InputManager) GetResponse() InputManagerResponse {
	return im.Response
}

// GetData returns the data if request was successful
//
// Returns:
//
//	string: The data from the response as JSON string, or empty string if request failed.
//	        Parse the JSON string to get the actual typed value.
func (im *InputManager) GetData() string {
	if im.Response.RequestStatusSet && im.Response.RequestStatus {
		return im.Response.Data
	}
	return ""
}

// OutputManager handles receiving requests from other processes
//
// This is a global singleton - all functions are package-level.
// Must call Init() before using.
//
// Functions:
//     Init(): Initialize and read request from stdin
//     GetData(): Get the request data as JSON string
//     Output(data): Send response back via stdout
//     Cleanup(): Clean up resources

type outputManagerData struct {
	originalStdout   *os.File
	requestJSON      string
	key              string
	data         string
	optionalOutput   bool
	isUnique         bool
	requestStatus    bool
	requestStatusSet bool
	uniqueState      bool
	uniqueStateSet   bool
	initError        bool
	errors           []string
	warnings         []string
}

var globalOutputManager *outputManagerData

// Init initializes the OutputManager and reads request from stdin
//
// Must be called before using Output() or GetData().
// Suppresses stdout to prevent pollution of JSON protocol.
func Init() {
	globalOutputManager = &outputManagerData{
		errors:   []string{},
		warnings: []string{},
	}

	// Suppress stdout by setting to nil (Go doesn't write to nil file)
	globalOutputManager.originalStdout = os.Stdout
	os.Stdout = nil

	// Read the entire stdin (the JSON request from InputManager)
	scanner := bufio.NewScanner(os.Stdin)
	var input strings.Builder
	for scanner.Scan() {
		input.WriteString(scanner.Text())
	}
	globalOutputManager.requestJSON = input.String()

	var requestData map[string]interface{}
	json.Unmarshal([]byte(globalOutputManager.requestJSON), &requestData)

	if key, ok := requestData["key"].(string); ok {
		globalOutputManager.key = key
	}

	if data, ok := requestData["data"]; ok {
		dataBytes, _ := json.Marshal(data)
		globalOutputManager.data = string(dataBytes)
	}

	if opt, ok := requestData["optionalOutput"].(bool); ok {
		globalOutputManager.optionalOutput = opt
	}

	if uniq, ok := requestData["isUnique"].(bool); ok {
		globalOutputManager.isUnique = uniq
	}

	// Reset state for new request
	globalOutputManager.errors = []string{}
	globalOutputManager.warnings = []string{}
	globalOutputManager.initError = false
	globalOutputManager.requestStatusSet = false
	globalOutputManager.uniqueStateSet = false
}

// GetData returns the request data with proper type conversion
//
// Returns:
//
//	any: The data with appropriate Go type (int for whole numbers, float64 for decimals, etc.)
func GetData() any {
	var result any
	if globalOutputManager != nil {
		json.Unmarshal([]byte(globalOutputManager.data), &result)
		// Convert float64 to int if it's a whole number
		if f, ok := result.(float64); ok {
			if f == float64(int(f)) {
				return int(f)
			}
		}
	}
	return result
}

// Bundle converts any data to a JSON string for use with Output()
//
// Parameters:
//
//	data: Any JSON-serializable value (string, int, float, bool, slice, map, struct)
//
// Returns:
//
//	string: JSON string representation of the data
func Bundle(data interface{}) string {
	jsonBytes, _ := json.Marshal(data)
	return string(jsonBytes)
}

// Output sends a response back to the calling process
//
// Parameters:
//
//	data: Data to send as JSON string (any JSON-serializable type)
//
// Note:
//
//	Can be called multiple times if isUnique=false in request.
//	Will error if called multiple times when isUnique=true.
func Output(data string) {
	// Check if OutputManager was initialized
	if globalOutputManager == nil || globalOutputManager.data == "" {
		if globalOutputManager != nil && !globalOutputManager.initError {
			// Restore original stdout to actually write the response
			os.Stdout = globalOutputManager.originalStdout

			globalOutputManager.requestStatus = false
			globalOutputManager.errors = append(globalOutputManager.errors, "Error: OutputManager isn't initialized.")

			// Build and write JSON response
			response := map[string]interface{}{
				"key":            nil,
				"request_status": false,
				"data":           nil,
				"optionalOutput": globalOutputManager.optionalOutput,
				"isUnique":       nil,
				"errors":         globalOutputManager.errors,
				"warnings":       globalOutputManager.warnings,
			}

			responseBytes, _ := json.Marshal(response)
			fmt.Fprintln(globalOutputManager.originalStdout, string(responseBytes))

			globalOutputManager.initError = true
		}
		return
	}

	// Check if we can output based on isUnique setting
	// uniqueStateSet tracks if we've already output once
	if !globalOutputManager.uniqueStateSet || !globalOutputManager.isUnique {
		globalOutputManager.requestStatus = true

		// Restore original stdout to actually write the response
		os.Stdout = globalOutputManager.originalStdout

		var parsed interface{}
		json.Unmarshal([]byte(data), &parsed)

		// Build and write JSON response
		response := map[string]interface{}{
			"key":            globalOutputManager.key,
			"request_status": true,
			"data":           parsed,
			"optionalOutput": globalOutputManager.optionalOutput,
			"isUnique":       globalOutputManager.isUnique,
			"errors":         []string{},
			"warnings":       []string{},
		}

		responseBytes, _ := json.Marshal(response)
		fmt.Fprintln(globalOutputManager.originalStdout, string(responseBytes))

	} else {
		// Multiple outputs when isUnique=true is an error
		globalOutputManager.requestStatus = false
		uniqueStateValue := globalOutputManager.uniqueState
		globalOutputManager.errors = append(globalOutputManager.errors, fmt.Sprintf("Error: outputs out of bound (isUnique: %v).", uniqueStateValue))

		// Restore original stdout
		os.Stdout = globalOutputManager.originalStdout

		var parsed interface{}
		json.Unmarshal([]byte(data), &parsed)

		response := map[string]interface{}{
			"key":            globalOutputManager.key,
			"request_status": false,
			"data":           parsed,
			"optionalOutput": globalOutputManager.optionalOutput,
			"isUnique":       globalOutputManager.isUnique,
			"errors":         globalOutputManager.errors,
			"warnings":       globalOutputManager.warnings,
		}

		responseBytes, _ := json.Marshal(response)
		fmt.Fprintln(globalOutputManager.originalStdout, string(responseBytes))
	}

	// Mark that we've output once
	globalOutputManager.uniqueState = globalOutputManager.isUnique
	globalOutputManager.uniqueStateSet = true

	// Re-suppress stdout after writing response
	os.Stdout = nil
}

// Cleanup cleans up OutputManager resources
func Cleanup() {
	if globalOutputManager != nil {
		globalOutputManager.errors = []string{}
		globalOutputManager.warnings = []string{}
	}
}
