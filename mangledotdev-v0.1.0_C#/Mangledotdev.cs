using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Mangledotdev
{
    /// <summary>
    /// InputManagerResponse - Contains the complete response structure
    ///
    /// Fields:
    ///     RequestStatusSet: Whether request status has been set
    ///     RequestStatus: Success status of the request
    ///     Data: Response data as JSON string (preserves type)
    ///     OptionalOutput: Echo of request parameter
    ///     IsUnique: Echo of request parameter
    ///     Warnings: List of warning messages
    ///     Errors: List of error messages
    /// </summary>
    public class InputManagerResponse
    {
        public bool RequestStatusSet { get; set; }
        public bool RequestStatus { get; set; }
        public string Data { get; set; }
        public bool OptionalOutput { get; set; }
        public bool IsUnique { get; set; }
        public List<string> Warnings { get; set; }
        public List<string> Errors { get; set; }

        public InputManagerResponse()
        {
            RequestStatusSet = false;
            RequestStatus = false;
            Data = "";
            OptionalOutput = true;
            IsUnique = true;
            Warnings = new List<string>();
            Errors = new List<string>();
        }
    }

    /// <summary>
    /// InputManager - Manages sending requests to other processes and handling responses
    ///
    /// This is an instance-based class - create one instance per request.
    ///
    /// Properties:
    ///     Response: Complete response with status, data, errors, warnings
    ///
    /// Methods:
    ///     Request(): Send a request to another process
    ///     GetResponse(): Get the full response object
    ///     GetData(): Get the response data (returns empty string on error)
    ///     Bundle(): Convert any value to JSON string
    /// </summary>
    public class InputManager
    {
        private string _key;
        private string _rawRequest;
        private string _request;
        private List<JsonNode> _responseData;

        public InputManagerResponse Response { get; set; }

        /// <summary>
        /// Create a new InputManager instance
        /// </summary>
        public InputManager()
        {
            _key = "";
            _rawRequest = "";
            _request = "";
            _responseData = new List<JsonNode>();
            Response = new InputManagerResponse();
        }

        /// <summary>
        /// Generate a unique key for request/response matching
        /// </summary>
        /// <returns>Unique key as 32-char hex string</returns>
        private static string GenKey()
        {
            byte[] bytes = RandomNumberGenerator.GetBytes(16);
            return Convert.ToHexString(bytes).ToLower();
        }

        /// <summary>
        /// Bundle any value to JSON string for use with Request()
        /// </summary>
        /// <param name="value">Any JSON-serializable value</param>
        /// <returns>JSON string representation</returns>
        public static string Bundle(object value)
        {
            return JsonSerializer.Serialize(value);
        }

        /// <summary>
        /// Validate file and build command to execute
        /// </summary>
        /// <param name="language">Programming language/runtime</param>
        /// <param name="file">Path to file to execute</param>
        /// <returns>Command string</returns>
        /// <exception cref="ArgumentException">Invalid file extension or file not found</exception>
        private string GetCommand(string language, string file)
        {
            string langUpper = language.ToUpper();

            // Normalize path separators for the current platform
            string filePath = file.Replace('/', Path.DirectorySeparatorChar);

            string ext = Path.GetExtension(filePath).ToLower();

            // Extension validation - FIRST before file existence check
            var extensionMap = new Dictionary<string, List<string>>
            {
                {"PYTHON", new List<string> {".py"}},
                {"PY", new List<string> {".py"}},
                {"JAVASCRIPT", new List<string> {".js"}},
                {"JS", new List<string> {".js"}},
                {"NODE", new List<string> {".js"}},
                {"NODEJS", new List<string> {".js"}},
                {"RUBY", new List<string> {".rb"}},
                {"RB", new List<string> {".rb"}},
                {"C", new List<string> {".c", ".out", ".exe", ""}},
                {"CS", new List<string> {".dll", ".exe", ""}},
                {"CPP", new List<string> {".cpp", ".cc", ".cxx", ".out", ".exe", ""}},
                {"C#", new List<string> {".dll", ".exe", ""}},
                {"C++", new List<string> {".cpp", ".cc", ".cxx", ".out", ".exe", ""}},
                {"CSHARP", new List<string> {".dll", ".exe", ""}},
                {"CPLUSPLUS", new List<string> {".cpp", ".cc", ".cxx", ".out", ".exe", ""}},
                {"EXE", new List<string> {".cpp", ".cc", ".cxx", ".out", ".exe", ""}},
                {"JAR", new List<string> {".jar"}},
                {"JAVA", new List<string> {".jar", ".class"}},
                {"RUST", new List<string> {".rs", ".exe", ".out", ""}},
                {"RS", new List<string> {".rs", ".exe", ".out", ""}},
                {"GO", new List<string> {".go", ".exe", ".out", ""}},
                {"GOLANG", new List<string> {".go", ".exe", ".out", ""}}
            };

            if (extensionMap.ContainsKey(langUpper))
            {
                var validExts = extensionMap[langUpper];
                if (!validExts.Contains(ext))
                {
                    string expected = string.Join(", ", validExts.Select(e => string.IsNullOrEmpty(e) ? "(no extension)" : e));
                    throw new ArgumentException($"Invalid file '{filePath}' for language '{language}'. Expected: e.g. 'file{expected}'");
                }
            }

            // File existence check
            if (!File.Exists(filePath))
            {
                throw new FileNotFoundException($"File not found: {filePath}");
            }

            // Permission checks
            var compiledLangs = new List<string> {"C", "CS", "CPP", "C#", "C++", "CSHARP", "CPLUSPLUS", "EXE", "RUST", "RS", "GO", "GOLANG"};
            // Note: C# doesn't have a reliable way to check execute permissions on Windows

            // Auto-add .\ or ./ for compiled executables if not present and not absolute path
            string finalPath = filePath;
            if (compiledLangs.Contains(langUpper) && !Path.IsPathRooted(filePath) &&
                !filePath.StartsWith("./") && !filePath.StartsWith(".\\") &&
                !filePath.StartsWith("." + Path.DirectorySeparatorChar))
            {
                finalPath = "." + Path.DirectorySeparatorChar + filePath;
            }

            // Build command
            var langMap = new Dictionary<string, string>
            {
                {"PYTHON", $"python {finalPath}"},
                {"PY", $"python {finalPath}"},
                {"JAVASCRIPT", $"node {finalPath}"},
                {"JS", $"node {finalPath}"},
                {"NODE", $"node {finalPath}"},
                {"NODEJS", $"node {finalPath}"},
                {"RUBY", $"ruby {finalPath}"},
                {"RB", $"ruby {finalPath}"},
                {"C", finalPath},
                {"CS", ext == ".dll" ? $"dotnet {finalPath}" : finalPath},
                {"CPP", finalPath},
                {"C#", ext == ".dll" ? $"dotnet {finalPath}" : finalPath},
                {"C++", finalPath},
                {"CSHARP", ext == ".dll" ? $"dotnet {finalPath}" : finalPath},
                {"CPLUSPLUS", finalPath},
                {"EXE", finalPath},
                {"JAR", $"java -jar {finalPath}"},
                {"JAVA", ext == ".jar" ? $"java -jar {finalPath}" : $"java -cp {Path.GetDirectoryName(finalPath) ?? "."} {Path.GetFileNameWithoutExtension(finalPath)}"},
                {"RUST", finalPath},
                {"RS", finalPath},
                {"GO", ext == ".go" ? $"go run {finalPath}" : finalPath},
                {"GOLANG", ext == ".go" ? $"go run {finalPath}" : finalPath}
            };

            if (!langMap.ContainsKey(langUpper))
            {
                throw new ArgumentException($"Unsupported language: {language}");
            }

            return langMap[langUpper];
        }

        /// <summary>
        /// Send a request to another process
        /// </summary>
        /// <param name="isUnique">Expect single output (true) or multiple (false)</param>
        /// <param name="optionalOutput">Output is optional (true) or required (false)</param>
        /// <param name="data">Data to send as JSON string</param>
        /// <param name="language">Target language/runtime</param>
        /// <param name="file">Path to target file</param>
        ///
        /// Sets Response property with fields:
        ///     - RequestStatus (bool): Success status
        ///     - Data (string): Response data as JSON string (preserves type)
        ///     - OptionalOutput (bool): Echo of parameter
        ///     - IsUnique (bool): Echo of parameter
        ///     - Warnings (List<string>): Warning messages
        ///     - Errors (List<string>): Error messages
        public void Request(bool isUnique = true, bool optionalOutput = true,
                          string data = "", string language = "", string file = "")
        {
            try
            {
                _key = GenKey();
                string command = GetCommand(language, file);

                // Clear previous response
                Response = new InputManagerResponse
                {
                    OptionalOutput = optionalOutput,
                    IsUnique = isUnique
                };

                // Build request JSON
                var requestObj = new JsonObject
                {
                    ["key"] = _key,
                    ["optionalOutput"] = optionalOutput,
                    ["isUnique"] = isUnique
                };

                if (!string.IsNullOrEmpty(data))
                {
                    try
                    {
                        requestObj["data"] = JsonNode.Parse(data);
                    }
                    catch
                    {
                        requestObj["data"] = null;
                    }
                }
                else
                {
                    requestObj["data"] = null;
                }

                string requestStr = requestObj.ToJsonString();

                // Parse command into executable and arguments
                string[] cmdParts = command.Split(' ', 2);
                string executable = cmdParts[0];
                string arguments = cmdParts.Length > 1 ? cmdParts[1] : "";

                ProcessStartInfo psi = new ProcessStartInfo
                {
                    FileName = executable,
                    Arguments = arguments,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardInput = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                };

                using (Process process = new Process { StartInfo = psi })
                {
                    process.Start();

                    // Write request to stdin
                    process.StandardInput.Write(requestStr);
                    process.StandardInput.Close();

                    // Read output from stdout
                    string output = process.StandardOutput.ReadToEnd();
                    string errors = process.StandardError.ReadToEnd();

                    process.WaitForExit();

                    // Check exit code
                    if (process.ExitCode != 0)
                    {
                        Response.RequestStatus = false;
                        Response.RequestStatusSet = true;
                        Response.Errors.Add($"Process exited with code {process.ExitCode}");
                        if (!string.IsNullOrEmpty(errors))
                        {
                            Response.Errors.Add($"stderr: {errors}");
                        }
                        Response.Warnings.Add("Warning: these kind of errors result from an error in the targeted script.");
                        return;
                    }

                    // Parse responses
                    string[] lines = output.Split('\n');
                    var responses = new List<JsonNode>();

                    foreach (string line in lines)
                    {
                        if (string.IsNullOrWhiteSpace(line)) continue;

                        try
                        {
                            JsonNode json = JsonNode.Parse(line);
                            if (json == null) continue;

                            // Validate response has matching key or null key (for init errors)
                            // This ensures we only process responses meant for this request
                            var keyItem = json["key"];
                            if (keyItem == null)
                            {
                                // Key is null (for init errors) - accept this response
                                responses.Add(json);
                            }
                            else
                            {
                                try
                                {
                                    string responseKey = keyItem.GetValue<string>();
                                    if (responseKey == _key)
                                    {
                                        responses.Add(json);
                                    }
                                }
                                catch
                                {
                                    // Invalid key format - ignore
                                }
                            }
                        }
                        catch
                        {
                            // Ignore lines that aren't valid JSON (e.g., debug prints)
                        }
                    }

                    // Process responses
                    if (responses.Count > 0)
                    {
                        bool failure = false;

                        foreach (var resp in responses)
                        {
                            var status = resp["request_status"];
                            if (status != null && status.GetValue<bool?>() == false)
                            {
                                failure = true;
                            }

                            var errorsList = resp["errors"]?.AsArray();
                            if (errorsList != null)
                            {
                                foreach (var error in errorsList)
                                {
                                    if (error != null)
                                    {
                                        Response.Errors.Add(error.GetValue<string>());
                                    }
                                }
                            }
                        }

                        Response.RequestStatus = !failure;
                        Response.RequestStatusSet = true;

                        // Collect data
                        if (isUnique && responses.Count == 1)
                        {
                            var respData = responses[0]["data"];
                            if (respData != null)
                            {
                                Response.Data = respData.ToJsonString();
                            }
                        }
                        else if (isUnique && responses.Count > 1)
                        {
                            Response.RequestStatus = false;
                            Response.Data = "";
                            Response.Errors.Add($"Error: Expected 1 output (isUnique=True) but received {responses.Count}.");
                        }
                        else
                        {
                            var dataArray = new JsonArray();
                            foreach (var resp in responses)
                            {
                                var respData = resp["data"];
                                if (respData != null)
                                {
                                    dataArray.Add(JsonNode.Parse(respData.ToJsonString()));
                                }
                            }
                            Response.Data = dataArray.ToJsonString();
                        }
                    }
                    else if (optionalOutput)
                    {
                        Response.RequestStatusSet = false;
                        Response.Warnings.Add("Warning: the output setting is set to optional, and the targeted program didn't gave any output.");
                    }
                    else
                    {
                        Response.RequestStatus = false;
                        Response.RequestStatusSet = true;
                        Response.Errors.Add("Error: OutputManager might not be used or not correctly.");
                    }
                }
            }
            catch (Exception e)
            {
                Response.RequestStatus = false;
                Response.RequestStatusSet = true;
                Response.OptionalOutput = optionalOutput;
                Response.IsUnique = isUnique;
                Response.Warnings.Add("Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies.");
                Response.Errors.Add($"Error: {e.Message}");
            }
        }

        /// <summary>
        /// Get the full response object
        /// </summary>
        /// <returns>The complete response with status, data, errors, warnings.</returns>
        public InputManagerResponse GetResponse()
        {
            return Response;
        }

        /// <summary>
        /// Get the response data if request was successful
        /// </summary>
        /// <returns>The data from the response as JSON string, or empty string if request failed.
        /// Parse the JSON string to get the actual typed value.</returns>
        public string GetData()
        {
            if (Response.RequestStatusSet && Response.RequestStatus)
            {
                return Response.Data;
            }
            return "";
        }
    }

    /// <summary>
    /// OutputManager - Manages receiving requests from other processes and sending responses
    ///
    /// This is a static class - all methods are static.
    /// Must call Init() before using.
    ///
    /// Static Properties:
    ///     Data: The request data as JSON string (accessible after Init())
    ///
    /// Static Methods:
    ///     Init(): Initialize and read request from stdin
    ///     GetData(): Get the request data as JSON string
    ///     GetInt(): Get the request data as integer
    ///     GetDouble(): Get the request data as double
    ///     GetString(): Get the request data as string
    ///     GetBool(): Get the request data as boolean
    ///     Bundle(): Convert any value to JSON string
    ///     Output(data): Send response back via stdout
    ///     Cleanup(): Clean up resources
    /// </summary>
    public static class OutputManager
    {
        private static TextWriter _originalStdout;
        private static string _requestJson;
        private static string _key;
        private static bool _optionalOutput;
        private static bool _isUnique;
        private static bool _requestStatusSet;
        private static bool _requestStatus;
        private static bool _uniqueStateSet;
        private static bool _uniqueState;
        private static bool _initError;
        private static List<string> _errors = new List<string>();
        private static List<string> _warnings = new List<string>();

        public static string Data { get; set; }

        /// <summary>
        /// Initialize OutputManager and read request from stdin
        ///
        /// Must be called before using Output() or accessing Data.
        /// Suppresses stdout to prevent pollution of JSON protocol.
        /// </summary>
        public static void Init()
        {
            // Save original stdout so we can restore it later
            _originalStdout = Console.Out;

            // Redirect stdout to TextWriter.Null to suppress all Console.WriteLine statements
            Console.SetOut(TextWriter.Null);

            // Read the entire stdin (the JSON request from InputManager)
            using (StreamReader reader = new StreamReader(Console.OpenStandardInput()))
            {
                _requestJson = reader.ReadToEnd();
            }

            // Parse JSON
            try
            {
                JsonNode json = JsonNode.Parse(_requestJson);
                if (json != null)
                {
                    var keyItem = json["key"];
                    if (keyItem != null) _key = keyItem.GetValue<string>();

                    var dataItem = json["data"];
                    if (dataItem != null)
                    {
                        Data = dataItem.ToJsonString();
                    }

                    var optItem = json["optionalOutput"];
                    if (optItem != null) _optionalOutput = optItem.GetValue<bool>();

                    var uniqItem = json["isUnique"];
                    if (uniqItem != null) _isUnique = uniqItem.GetValue<bool>();
                }
            }
            catch
            {
                // Parsing error
            }

            // Reset state for new request
            _errors.Clear();
            _warnings.Clear();
            _initError = false;
            _requestStatusSet = false;
            _uniqueStateSet = false;
        }

        /// <summary>
        /// Get the request data as JSON string
        /// </summary>
        /// <returns>Request data as JSON string (parse to get typed value)</returns>
        public static string GetData()
        {
            return Data;
        }

        /// <summary>
        /// Get the request data as integer
        /// </summary>
        /// <returns>The data as int (0 if not a number)</returns>
        public static int GetInt()
        {
            if (string.IsNullOrEmpty(Data)) return 0;
            try
            {
                var node = JsonNode.Parse(Data);
                if (node != null)
                {
                    return node.GetValue<int>();
                }
            }
            catch { }
            return 0;
        }

        /// <summary>
        /// Get the request data as double
        /// </summary>
        /// <returns>The data as double (0.0 if not a number)</returns>
        public static double GetDouble()
        {
            if (string.IsNullOrEmpty(Data)) return 0.0;
            try
            {
                var node = JsonNode.Parse(Data);
                if (node != null)
                {
                    return node.GetValue<double>();
                }
            }
            catch { }
            return 0.0;
        }

        /// <summary>
        /// Get the request data as string
        /// </summary>
        /// <returns>The data as string (empty string if not a string)</returns>
        public static string GetString()
        {
            if (string.IsNullOrEmpty(Data)) return "";
            try
            {
                var node = JsonNode.Parse(Data);
                if (node != null)
                {
                    return node.GetValue<string>();
                }
            }
            catch { }
            return "";
        }

        /// <summary>
        /// Get the request data as boolean
        /// </summary>
        /// <returns>The data as bool (false if not a boolean)</returns>
        public static bool GetBool()
        {
            if (string.IsNullOrEmpty(Data)) return false;
            try
            {
                var node = JsonNode.Parse(Data);
                if (node != null)
                {
                    return node.GetValue<bool>();
                }
            }
            catch { }
            return false;
        }

        /// <summary>
        /// Bundle any value to JSON string for use with Output()
        /// </summary>
        /// <param name="value">Any JSON-serializable value</param>
        /// <returns>JSON string representation</returns>
        public static string Bundle(object value)
        {
            return JsonSerializer.Serialize(value);
        }

        /// <summary>
        /// Send response back to the calling process
        /// </summary>
        /// <param name="data">Data to send as JSON string</param>
        ///
        /// Note:
        ///     Can be called multiple times if isUnique=false in request.
        ///     Will error if called multiple times when isUnique=true.
        public static void Output(string data)
        {
            // Check if OutputManager was initialized
            if (string.IsNullOrEmpty(Data))
            {
                if (!_initError)
                {
                    // Restore original stdout to actually write the response
                    if (_originalStdout != null)
                    {
                        Console.SetOut(_originalStdout);
                    }

                    _requestStatus = false;
                    _errors.Add("Error: OutputManager isn't initialized.");

                    // Build and write JSON response
                    var response = new JsonObject
                    {
                        ["key"] = null,
                        ["request_status"] = false,
                        ["data"] = null,
                        ["optionalOutput"] = _optionalOutput,
                        ["isUnique"] = null,
                        ["errors"] = new JsonArray(_errors.Select(e => JsonValue.Create(e)).ToArray()),
                        ["warnings"] = new JsonArray()
                    };

                    Console.WriteLine(response.ToJsonString());
                    Console.Out.Flush();

                    _initError = true;
                }
                return;
            }

            // Check if we can output based on isUnique setting
            // uniqueStateSet tracks if we've already output once
            if (!_uniqueStateSet || !_isUnique)
            {
                _requestStatus = true;

                // Restore original stdout to actually write the response
                if (_originalStdout != null)
                {
                    Console.SetOut(_originalStdout);
                }

                // Build and write JSON response
                var response = new JsonObject
                {
                    ["key"] = _key,
                    ["request_status"] = true,
                    ["optionalOutput"] = _optionalOutput,
                    ["isUnique"] = _isUnique,
                    ["errors"] = new JsonArray(),
                    ["warnings"] = new JsonArray()
                };

                try
                {
                    response["data"] = JsonNode.Parse(data);
                }
                catch
                {
                    response["data"] = null;
                }

                Console.WriteLine(response.ToJsonString());
                Console.Out.Flush();
            }
            else
            {
                // Multiple outputs when isUnique=true is an error
                _requestStatus = false;
                _errors.Add($"Error: outputs out of bound (isUnique: {_uniqueState}).");

                // Restore original stdout
                if (_originalStdout != null)
                {
                    Console.SetOut(_originalStdout);
                }

                var response = new JsonObject
                {
                    ["key"] = _key,
                    ["request_status"] = false,
                    ["optionalOutput"] = _optionalOutput,
                    ["isUnique"] = _isUnique,
                    ["errors"] = new JsonArray(_errors.Select(e => JsonValue.Create(e)).ToArray()),
                    ["warnings"] = new JsonArray()
                };

                try
                {
                    response["data"] = JsonNode.Parse(data);
                }
                catch
                {
                    response["data"] = null;
                }

                Console.WriteLine(response.ToJsonString());
                Console.Out.Flush();
            }

            // Mark that we've output once
            _uniqueState = _isUnique;
            _uniqueStateSet = true;

            // Re-suppress stdout after writing response
            Console.SetOut(TextWriter.Null);
        }

        /// <summary>
        /// Clean up OutputManager resources
        /// </summary>
        public static void Cleanup()
        {
            _errors.Clear();
            _warnings.Clear();
        }
    }
}