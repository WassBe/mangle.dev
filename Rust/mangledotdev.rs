use serde_json::{json, Value};
use std::collections::HashMap;
use std::fs;
use std::io::{self, Read, Write};
use std::path::Path;
use std::process::{Command, Stdio};

// InputManagerResponse structure
//
// Contains the complete response with status, data, errors, and warnings
#[derive(Debug, Clone)]
pub struct InputManagerResponse {
    pub request_status_set: bool,
    pub request_status: bool,
    pub data: String,
    pub optional_output: bool,
    pub is_unique: bool,
    pub warnings: Vec<String>,
    pub errors: Vec<String>,
}

impl InputManagerResponse {
    fn new() -> Self {
        InputManagerResponse {
            request_status_set: false,
            request_status: false,
            data: String::new(),
            optional_output: true,
            is_unique: true,
            warnings: Vec::new(),
            errors: Vec::new(),
        }
    }
}

// InputManager - Manages sending requests to other processes and handling responses
//
// This is an instance-based struct - create one instance per request.
//
// Fields:
//     response: Complete response with status, data, errors, warnings
//
// Methods:
//     new(): Create a new InputManager instance
//     request(): Send a request to another process
//     get_response(): Get the response data (returns empty string on error)
pub struct InputManager {
    key: String,
    raw_request: HashMap<String, Value>,
    request: String,
    response_obj: Vec<Value>,
    pub response: InputManagerResponse,
}

impl InputManager {
    /// Create a new InputManager instance
    pub fn new() -> Self {
        InputManager {
            key: String::new(),
            raw_request: HashMap::new(),
            request: String::new(),
            response_obj: Vec::new(),
            response: InputManagerResponse::new(),
        }
    }

    /// Generate a unique key for request/response matching
    fn gen_key() -> String {
        let bytes: [u8; 16] = rand::random();
        bytes.iter().map(|b| format!("{:02x}", b)).collect()
    }

    /// Validate file and build command to execute
    ///
    /// # Arguments
    /// * `language` - Programming language/runtime
    /// * `file` - Path to file to execute
    ///
    /// # Returns
    /// * `Result<Vec<String>, String>` - Command array or error message
    fn get_command(&self, language: &str, file: &str) -> Result<Vec<String>, String> {
        let lang_upper = language.to_uppercase();
        let file_ext = Path::new(file)
            .extension()
            .and_then(|s| s.to_str())
            .unwrap_or("")
            .to_lowercase();

        // Extension validation - FIRST before file existence check
        let mut extension_map: HashMap<&str, Vec<&str>> = HashMap::new();
        extension_map.insert("PYTHON", vec![".py"]);
        extension_map.insert("PY", vec![".py"]);
        extension_map.insert("JAVASCRIPT", vec![".js"]);
        extension_map.insert("JS", vec![".js"]);
        extension_map.insert("NODE", vec![".js"]);
        extension_map.insert("NODEJS", vec![".js"]);
        extension_map.insert("RUBY", vec![".rb"]);
        extension_map.insert("RB", vec![".rb"]);
        extension_map.insert("C", vec![".c", ".out", ".exe", ""]);
        extension_map.insert("CS", vec![".exe", ".dll", ""]);
        extension_map.insert("CPP", vec![".cpp", ".cc", ".cxx", ".out", ".exe", ""]);
        extension_map.insert("C#", vec![".exe", ".dll", ""]);
        extension_map.insert("C++", vec![".cpp", ".cc", ".cxx", ".out", ".exe", ""]);
        extension_map.insert("CSHARP", vec![".exe", ".dll", ""]);
        extension_map.insert("CPLUSPLUS", vec![".cpp", ".cc", ".cxx", ".out", ".exe", ""]);
        extension_map.insert("EXE", vec![".cpp", ".cc", ".cxx", ".out", ".exe", ""]);
        extension_map.insert("JAR", vec![".jar"]);
        extension_map.insert("JAVA", vec![".jar"]);
        extension_map.insert("RUST", vec![".rs", ".exe", ".out", ""]);
        extension_map.insert("RS", vec![".rs", ".exe", ".out", ""]);
        extension_map.insert("GO", vec![".go", ".exe", ".out", ""]);
        extension_map.insert("GOLANG", vec![".go", ".exe", ".out", ""]);

        if let Some(valid_exts) = extension_map.get(lang_upper.as_str()) {
            let ext_with_dot = format!(".{}", file_ext);
            if !valid_exts.contains(&ext_with_dot.as_str()) && !valid_exts.contains(&"") {
                let expected: Vec<String> = valid_exts
                    .iter()
                    .map(|e| if e.is_empty() { "(no extension)".to_string() } else { e.to_string() })
                    .collect();
                return Err(format!(
                    "Invalid file '{}' for language '{}'. Expected: e.g. 'file{}'",
                    file,
                    language,
                    expected.join(", ")
                ));
            }
        }

        // File existence check
        if !Path::new(file).exists() {
            return Err(format!("File not found: {}", file));
        }

        let metadata = fs::metadata(file).map_err(|e| e.to_string())?;
        if !metadata.is_file() {
            return Err(format!("Path is not a file: {}", file));
        }

        let compiled_langs = vec![
            "C", "CS", "CPP", "C#", "C++", "CSHARP", "CPLUSPLUS", "EXE", "RUST", "RS", "GO", "GOLANG",
        ];

        // Permission check - only on Unix systems
        #[cfg(unix)]
        {
            if compiled_langs.contains(&lang_upper.as_str()) {
                use std::os::unix::fs::PermissionsExt;
                let permissions = metadata.permissions();
                if permissions.mode() & 0o111 == 0 {
                    return Err(format!("File is not executable: {}", file));
                }
            }
        }

        // Auto-add ./ for compiled executables if not present and not absolute path
        let mut file = file.to_string();
        if compiled_langs.contains(&lang_upper.as_str()) 
            && !Path::new(&file).is_absolute() 
            && !file.starts_with("./") 
            && !file.starts_with(".\\") {
            file = format!("./{}", file);
        }

        // Build command
        let mut lang_map: HashMap<&str, Vec<String>> = HashMap::new();
        lang_map.insert("PYTHON", vec!["python".to_string(), file.clone()]);
        lang_map.insert("PY", vec!["python".to_string(), file.clone()]);
        lang_map.insert("JAVASCRIPT", vec!["node".to_string(), file.clone()]);
        lang_map.insert("JS", vec!["node".to_string(), file.clone()]);
        lang_map.insert("NODE", vec!["node".to_string(), file.clone()]);
        lang_map.insert("NODEJS", vec!["node".to_string(), file.clone()]);
        lang_map.insert("RUBY", vec!["ruby".to_string(), file.clone()]);
        lang_map.insert("RB", vec!["ruby".to_string(), file.clone()]);
        lang_map.insert("C", vec![file.clone()]);
        lang_map.insert("CS", vec![file.clone()]);
        lang_map.insert("CPP", vec![file.clone()]);
        lang_map.insert("C#", vec![file.clone()]);
        lang_map.insert("C++", vec![file.clone()]);
        lang_map.insert("CSHARP", vec![file.clone()]);
        lang_map.insert("CPLUSPLUS", vec![file.clone()]);
        lang_map.insert("EXE", vec![file.clone()]);
        lang_map.insert("JAR", vec!["java".to_string(), "-jar".to_string(), file.clone()]);
        lang_map.insert("JAVA", vec!["java".to_string(), "-jar".to_string(), file.clone()]);
        lang_map.insert("RUST", vec![file.clone()]);
        lang_map.insert("RS", vec![file.clone()]);

        if file_ext == "go" {
            lang_map.insert("GO", vec!["go".to_string(), "run".to_string(), file.clone()]);
            lang_map.insert("GOLANG", vec!["go".to_string(), "run".to_string(), file.clone()]);
        } else {
            lang_map.insert("GO", vec![file.clone()]);
            lang_map.insert("GOLANG", vec![file.clone()]);
        }

        lang_map
            .get(lang_upper.as_str())
            .cloned()
            .ok_or_else(|| format!("Unsupported language: {}", language))
    }

    /// Send a request to another process
    ///
    /// # Arguments
    /// * `is_unique` - Expect single output (true) or multiple (false)
    /// * `optional_output` - Output is optional (true) or required (false)
    /// * `data` - Data to send as JSON string
    /// * `language` - Target language/runtime
    /// * `file` - Path to target file
    ///
    /// Sets self.response with fields:
    ///     - request_status (bool): Success status
    ///     - data (String): Response data as JSON string
    ///     - optional_output (bool): Echo of parameter
    ///     - is_unique (bool): Echo of parameter
    ///     - warnings (Vec<String>): Warning messages
    ///     - errors (Vec<String>): Error messages
    pub fn request(
        &mut self,
        is_unique: bool,
        optional_output: bool,
        data: &str,
        language: &str,
        file: &str,
    ) {
        self.key = Self::gen_key();

        let command = match self.get_command(language, file) {
            Ok(cmd) => cmd,
            Err(e) => {
                self.response = InputManagerResponse {
                    request_status_set: true,
                    request_status: false,
                    data: String::new(),
                    optional_output,
                    is_unique,
                    warnings: vec!["Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies.".to_string()],
                    errors: vec![format!("Error: {}", e)],
                };
                return;
            }
        };

        self.response = InputManagerResponse {
            request_status_set: false,
            request_status: false,
            data: String::new(),
            optional_output,
            is_unique,
            warnings: Vec::new(),
            errors: Vec::new(),
        };

        let data_value: Value = if !data.is_empty() {
            serde_json::from_str(data).unwrap_or(Value::Null)
        } else {
            Value::Null
        };

        let request_obj = json!({
            "key": self.key,
            "optionalOutput": optional_output,
            "isUnique": is_unique,
            "data": data_value
        });

        self.request = request_obj.to_string();

        let mut child = match Command::new(&command[0])
            .args(&command[1..])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
        {
            Ok(c) => c,
            Err(e) => {
                self.response.request_status = false;
                self.response.request_status_set = true;
                self.response.errors.push(format!("Failed to start process: {}", e));
                return;
            }
        };

        if let Some(mut stdin) = child.stdin.take() {
            let _ = stdin.write_all(self.request.as_bytes());
        }

        let output = match child.wait_with_output() {
            Ok(o) => o,
            Err(e) => {
                self.response.request_status = false;
                self.response.request_status_set = true;
                self.response.errors.push(format!("Process error: {}", e));
                return;
            }
        };

        if !output.status.success() {
            self.response.request_status = false;
            self.response.request_status_set = true;
            self.response
                .errors
                .push(format!("Process exited with code {:?}", output.status.code()));
            if !output.stderr.is_empty() {
                self.response.errors.push(format!(
                    "stderr: {}",
                    String::from_utf8_lossy(&output.stderr)
                ));
            }
            self.response.warnings.push(
                "Warning: these kind of errors result from an error in the targeted script."
                    .to_string(),
            );
            return;
        }

        let stdout_str = String::from_utf8_lossy(&output.stdout);
        self.response_obj.clear();

        for line in stdout_str.lines() {
            if line.trim().is_empty() {
                continue;
            }

            if let Ok(json_data) = serde_json::from_str::<Value>(line) {
                if let Some(obj) = json_data.as_object() {
                    // Validate response has matching key or null key (for init errors)
                    // This ensures we only process responses meant for this request
                    if let Some(key_val) = obj.get("key") {
                        let matches = if key_val.is_null() {
                            true
                        } else if let Some(k) = key_val.as_str() {
                            k == self.key
                        } else {
                            false
                        };

                        if matches {
                            self.response_obj.push(json_data.clone());
                        }
                    }
                }
            }
        }

        if !self.response_obj.is_empty() {
            let mut failure = false;

            for resp in &self.response_obj {
                if let Some(status) = resp.get("request_status").and_then(|s| s.as_bool()) {
                    if !status {
                        failure = true;
                    }
                }

                if let Some(errors) = resp.get("errors").and_then(|e| e.as_array()) {
                    for err in errors {
                        if let Some(err_str) = err.as_str() {
                            self.response.errors.push(err_str.to_string());
                        }
                    }
                }
            }

            self.response.request_status = !failure;
            self.response.request_status_set = true;

            if let Some(is_uniq) = self.response_obj[0].get("isUnique").and_then(|u| u.as_bool()) {
                self.response.is_unique = is_uniq;
            }

            let data_list: Vec<&Value> = self.response_obj.iter().map(|r| r.get("data").unwrap_or(&Value::Null)).collect();

            if self.response.is_unique {
                if data_list.len() == 1 {
                    self.response.data = data_list[0].to_string();
                } else {
                    self.response.request_status = false;
                    self.response.data = String::new();
                    self.response.errors.push(format!(
                        "Error: Expected 1 output (isUnique=True) but received {}.",
                        data_list.len()
                    ));
                }
            } else {
                self.response.data = json!(data_list).to_string();
            }
        } else if optional_output {
            self.response.request_status_set = false;
            self.response.warnings.push(
                "Warning: the output setting is set to optional, and the targeted program didn't gave any output."
                    .to_string(),
            );
        } else {
            self.response.request_status = false;
            self.response.request_status_set = true;
            self.response
                .errors
                .push("Error: OutputManager might not be used or not correctly.".to_string());
        }
    }

    /// Get the full response object
    ///
    /// # Returns
    /// Reference to the complete response with status, data, errors, warnings
    pub fn get_response(&self) -> &InputManagerResponse {
        &self.response
    }

    /// Get the response data if request was successful
    ///
    /// # Returns
    /// String containing JSON data, or empty string if request failed
    pub fn get_data(&self) -> String {
        if self.response.request_status_set && self.response.request_status {
            self.response.data.clone()
        } else {
            String::new()
        }
    }

    /// Bundle any serializable value to JSON string for use with request()
    ///
    /// # Arguments
    /// * `value` - Any value that implements Serialize
    ///
    /// # Returns
    /// JSON string representation
    pub fn bundle<T: serde::Serialize>(value: T) -> String {
        serde_json::to_string(&value).unwrap_or_else(|_| "null".to_string())
    }
}

// OutputManager - Manages receiving requests from other processes and sending responses
//
// This uses static variables via lazy_static - all functions are module-level.
// Must call init() before using.
//
// Functions:
//     init(): Initialize and read request from stdin
//     get_data(): Get the request data as JSON string
//     output(data): Send response back via stdout
//     cleanup(): Clean up resources

use std::sync::Mutex;
use lazy_static::lazy_static;

lazy_static! {
    static ref OUTPUT_MANAGER: Mutex<OutputManagerData> = Mutex::new(OutputManagerData::new());
}

struct OutputManagerData {
    original_stdout: bool,
    request_json: String,
    key: String,
    data: String,
    optional_output: bool,
    is_unique: bool,
    request_status: bool,
    request_status_set: bool,
    unique_state: bool,
    unique_state_set: bool,
    init_error: bool,
    errors: Vec<String>,
    warnings: Vec<String>,
}

impl OutputManagerData {
    fn new() -> Self {
        OutputManagerData {
            original_stdout: false,
            request_json: String::new(),
            key: String::new(),
            data: String::new(),
            optional_output: true,
            is_unique: true,
            request_status: false,
            request_status_set: false,
            unique_state: false,
            unique_state_set: false,
            init_error: false,
            errors: Vec::new(),
            warnings: Vec::new(),
        }
    }
}

/// Initialize OutputManager and read request from stdin
///
/// Must be called before using output() or get_data().
/// Suppresses stdout to prevent pollution of JSON protocol.
pub fn init() {
    let mut manager = OUTPUT_MANAGER.lock().unwrap();
    
    // Mark that we've saved stdout (Rust doesn't allow redirecting it easily)
    manager.original_stdout = true;

    // Read the entire stdin (the JSON request from InputManager)
    let mut buffer = String::new();
    io::stdin().read_to_string(&mut buffer).unwrap();
    manager.request_json = buffer;

    if let Ok(request_data) = serde_json::from_str::<Value>(&manager.request_json) {
        if let Some(k) = request_data.get("key").and_then(|k| k.as_str()) {
            manager.key = k.to_string();
        }

        if let Some(data) = request_data.get("data") {
            manager.data = data.to_string();
        }

        if let Some(opt) = request_data.get("optionalOutput").and_then(|o| o.as_bool()) {
            manager.optional_output = opt;
        }

        if let Some(uniq) = request_data.get("isUnique").and_then(|u| u.as_bool()) {
            manager.is_unique = uniq;
        }
    }

    // Reset state for new request
    manager.errors.clear();
    manager.warnings.clear();
    manager.init_error = false;
    manager.request_status_set = false;
    manager.unique_state_set = false;
}

/// Get the request data as JSON string
///
/// # Returns
/// String containing the request data as JSON
pub fn get_data() -> String {
    let manager = OUTPUT_MANAGER.lock().unwrap();
    manager.data.clone()
}

/// Get the request data as integer
///
/// # Returns
/// The data as i64 (returns whole number, 0 if not a number)
pub fn get_int() -> i64 {
    let manager = OUTPUT_MANAGER.lock().unwrap();
    if let Ok(val) = serde_json::from_str::<Value>(&manager.data) {
        if let Some(f) = val.as_f64() {
            return f as i64;
        }
        if let Some(i) = val.as_i64() {
            return i;
        }
    }
    0
}

/// Get the request data as float
///
/// # Returns
/// The data as f64 (0.0 if not a number)
pub fn get_float() -> f64 {
    let manager = OUTPUT_MANAGER.lock().unwrap();
    if let Ok(val) = serde_json::from_str::<Value>(&manager.data) {
        if let Some(f) = val.as_f64() {
            return f;
        }
    }
    0.0
}

/// Get the request data as string
///
/// # Returns
/// The data as String (empty string if not a string)
pub fn get_string() -> String {
    let manager = OUTPUT_MANAGER.lock().unwrap();
    if let Ok(val) = serde_json::from_str::<Value>(&manager.data) {
        if let Some(s) = val.as_str() {
            return s.to_string();
        }
    }
    String::new()
}

/// Get the request data as boolean
///
/// # Returns
/// The data as bool (false if not a boolean)
pub fn get_bool() -> bool {
    let manager = OUTPUT_MANAGER.lock().unwrap();
    if let Ok(val) = serde_json::from_str::<Value>(&manager.data) {
        if let Some(b) = val.as_bool() {
            return b;
        }
    }
    false
}

/// Bundle any serializable value to JSON string for use with output()
///
/// # Arguments
/// * `value` - Any value that implements Serialize
///
/// # Returns
/// JSON string representation
pub fn bundle<T: serde::Serialize>(value: T) -> String {
    serde_json::to_string(&value).unwrap_or_else(|_| "null".to_string())
}

/// Send response back to the calling process
///
/// # Arguments
/// * `data` - Data to send as JSON string
///
/// Note:
///     Can be called multiple times if isUnique=false in request.
///     Will error if called multiple times when isUnique=true.
pub fn output(data: &str) {
    let mut manager = OUTPUT_MANAGER.lock().unwrap();

    // Check if OutputManager was initialized
    if manager.data.is_empty() {
        if !manager.init_error {
            manager.request_status = false;
            manager.errors.push("Error: OutputManager isn't initialized.".to_string());

            // Build and write JSON response
            let response = json!({
                "key": Value::Null,
                "request_status": false,
                "data": Value::Null,
                "optionalOutput": manager.optional_output,
                "isUnique": Value::Null,
                "errors": manager.errors,
                "warnings": manager.warnings,
            });

            println!("{}", response);
            manager.init_error = true;
        }
        return;
    }

    // Check if we can output based on isUnique setting
    // unique_state_set tracks if we've already output once
    if !manager.unique_state_set || !manager.is_unique {
        manager.request_status = true;

        let data_value: Value = serde_json::from_str(data).unwrap_or(Value::Null);

        // Build and write JSON response
        let response = json!({
            "key": manager.key,
            "request_status": true,
            "data": data_value,
            "optionalOutput": manager.optional_output,
            "isUnique": manager.is_unique,
            "errors": [],
            "warnings": [],
        });

        println!("{}", response);
    } else {
        // Multiple outputs when isUnique=true is an error
        manager.request_status = false;
        // CRITICAL FIX: Store value before using in format! (Rust borrowing rules)
        let unique_state_value = manager.unique_state;
        manager.errors.push(format!("Error: outputs out of bound (isUnique: {}).", unique_state_value));

        let data_value: Value = serde_json::from_str(data).unwrap_or(Value::Null);

        let response = json!({
            "key": manager.key,
            "request_status": false,
            "data": data_value,
            "optionalOutput": manager.optional_output,
            "isUnique": manager.is_unique,
            "errors": manager.errors,
            "warnings": manager.warnings,
        });

        println!("{}", response);
    }

    // Mark that we've output once
    manager.unique_state = manager.is_unique;
    manager.unique_state_set = true;
}

/// Clean up OutputManager resources
pub fn cleanup() {
    let mut manager = OUTPUT_MANAGER.lock().unwrap();
    manager.errors.clear();
    manager.warnings.clear();
}