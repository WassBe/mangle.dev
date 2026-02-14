import java.io.*;
import java.nio.file.*;
import java.security.SecureRandom;
import java.util.*;
import com.google.gson.*;

/**
 * InputManagerResponse - Contains the complete response structure
 * 
 * Fields:
 *     requestStatusSet: Whether request status has been set
 *     requestStatus: Success status of the request
 *     data: Response data as JSON string (preserves type)
 *     optionalOutput: Echo of request parameter
 *     isUnique: Echo of request parameter
 *     warnings: List of warning messages
 *     errors: List of error messages
 */
class InputManagerResponse {
    public boolean requestStatusSet;
    public boolean requestStatus;
    public String data;
    public boolean optionalOutput;
    public boolean isUnique;
    public List<String> warnings;
    public List<String> errors;

    public InputManagerResponse() {
        this.requestStatusSet = false;
        this.requestStatus = false;
        this.data = "";
        this.optionalOutput = true;
        this.isUnique = true;
        this.warnings = new ArrayList<>();
        this.errors = new ArrayList<>();
    }
}

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
    private String key;
    private String rawRequest;
    private String request;
    private List<JsonObject> responseData;
    
    public InputManagerResponse response;

    /**
     * Create a new InputManager instance
     */
    public InputManager() {
        this.key = "";
        this.rawRequest = "";
        this.request = "";
        this.responseData = new ArrayList<>();
        this.response = new InputManagerResponse();
    }

    /**
     * Generate a unique key for request/response matching
     *
     * @return Unique key as 32-char hex string
     */
    private static String genKey() {
        byte[] bytes = new byte[16];
        new SecureRandom().nextBytes(bytes);
        StringBuilder sb = new StringBuilder(32);
        for (byte b : bytes) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }

    /**
     * Validate file and build command to execute
     * 
     * @param language Programming language/runtime
     * @param file Path to file to execute
     * @return Command array for ProcessBuilder
     * @throws Exception Invalid file extension, file not found, or permission error
     */
    private List<String> getCommand(String language, String file) throws Exception {
        String langUpper = language.toUpperCase();
        String ext = "";
        int dotIndex = file.lastIndexOf('.');
        if (dotIndex > 0) {
            ext = file.substring(dotIndex).toLowerCase();
        }

        // Extension validation - FIRST before file existence check
        Map<String, List<String>> extensionMap = new HashMap<>();
        extensionMap.put("PYTHON", Arrays.asList(".py"));
        extensionMap.put("PY", Arrays.asList(".py"));
        extensionMap.put("JAVASCRIPT", Arrays.asList(".js"));
        extensionMap.put("JS", Arrays.asList(".js"));
        extensionMap.put("NODE", Arrays.asList(".js"));
        extensionMap.put("NODEJS", Arrays.asList(".js"));
        extensionMap.put("RUBY", Arrays.asList(".rb"));
        extensionMap.put("RB", Arrays.asList(".rb"));
        extensionMap.put("C", Arrays.asList(".c", ".out", ".exe", ""));
        extensionMap.put("CS", Arrays.asList(".exe", ".dll", ""));
        extensionMap.put("CPP", Arrays.asList(".cpp", ".cc", ".cxx", ".out", ".exe", ""));
        extensionMap.put("C#", Arrays.asList(".exe", ".dll", ""));
        extensionMap.put("C++", Arrays.asList(".cpp", ".cc", ".cxx", ".out", ".exe", ""));
        extensionMap.put("CSHARP", Arrays.asList(".exe", ".dll", ""));
        extensionMap.put("CPLUSPLUS", Arrays.asList(".cpp", ".cc", ".cxx", ".out", ".exe", ""));
        extensionMap.put("EXE", Arrays.asList(".cpp", ".cc", ".cxx", ".out", ".exe", ""));
        extensionMap.put("JAVA", Arrays.asList(".jar", ".class"));
        extensionMap.put("JAR", Arrays.asList(".jar", ".class"));
        extensionMap.put("RUST", Arrays.asList(".rs", ".exe", ".out", ""));
        extensionMap.put("RS", Arrays.asList(".rs", ".exe", ".out", ""));
        extensionMap.put("GO", Arrays.asList(".go", ".exe", ".out", ""));
        extensionMap.put("GOLANG", Arrays.asList(".go", ".exe", ".out", ""));

        if (extensionMap.containsKey(langUpper)) {
            List<String> validExts = extensionMap.get(langUpper);
            if (!validExts.contains(ext)) {
                String expected = String.join(", ", validExts.stream()
                    .map(e -> e.isEmpty() ? "(no extension)" : e)
                    .toArray(String[]::new));
                throw new IllegalArgumentException("Invalid file '" + file + "' for language '" + language + 
                    "'. Expected: e.g. 'file" + expected + "'");
            }
        }

        // File existence check
        File f = new File(file);
        if (!f.exists()) {
            throw new FileNotFoundException("File not found: " + file);
        }

        if (!f.isFile()) {
            throw new IllegalArgumentException("Path is not a file: " + file);
        }

        // Permission checks
        List<String> compiledLangs = Arrays.asList("C", "CS", "CPP", "C#", "C++", "CSHARP", "CPLUSPLUS", "EXE", "RUST", "RS", "GO", "GOLANG");
        if (compiledLangs.contains(langUpper)) {
            if (!f.canExecute()) {
                throw new IllegalArgumentException("File is not executable: " + file);
            }
        } else {
            if (!f.canRead()) {
                throw new IllegalArgumentException("File is not readable: " + file);
            }
        }

        // Auto-add ./ for compiled executables if not present and not absolute path
        if (compiledLangs.contains(langUpper) && !f.isAbsolute() && !file.startsWith("./") && !file.startsWith(".\\")) {
            file = "./" + file;
        }

        // Build command
        Map<String, List<String>> langMap = new HashMap<>();
        langMap.put("PYTHON", Arrays.asList("python", file));
        langMap.put("PY", Arrays.asList("python", file));
        langMap.put("JAVASCRIPT", Arrays.asList("node", file));
        langMap.put("JS", Arrays.asList("node", file));
        langMap.put("NODE", Arrays.asList("node", file));
        langMap.put("NODEJS", Arrays.asList("node", file));
        langMap.put("RUBY", Arrays.asList("ruby", file));
        langMap.put("RB", Arrays.asList("ruby", file));
        langMap.put("C", Arrays.asList(file));
        langMap.put("CS", Arrays.asList(file));
        langMap.put("CPP", Arrays.asList(file));
        langMap.put("C#", Arrays.asList(file));
        langMap.put("C++", Arrays.asList(file));
        langMap.put("CSHARP", Arrays.asList(file));
        langMap.put("CPLUSPLUS", Arrays.asList(file));
        langMap.put("EXE", Arrays.asList(file));
        if (ext.equals(".jar")) {
            langMap.put("JAVA", Arrays.asList("java", "-jar", file));
            langMap.put("JAR", Arrays.asList("java", "-jar", file));
        } else {
            // For .class files, extract directory and class name
            // Reuse existing 'f' variable from line 133
            String dir = f.getParent();
            String className = f.getName().replace(".class", "");
            langMap.put("JAVA", Arrays.asList("java", "-cp", dir != null ? dir : ".", className));
            langMap.put("JAR", Arrays.asList("java", "-cp", dir != null ? dir : ".", className));
        }
        langMap.put("RUST", Arrays.asList(file));
        langMap.put("RS", Arrays.asList(file));
        
        if (ext.equals(".go")) {
            langMap.put("GO", Arrays.asList("go", "run", file));
            langMap.put("GOLANG", Arrays.asList("go", "run", file));
        } else {
            langMap.put("GO", Arrays.asList(file));
            langMap.put("GOLANG", Arrays.asList(file));
        }

        if (!langMap.containsKey(langUpper)) {
            throw new IllegalArgumentException("Unsupported language: " + language);
        }

        return langMap.get(langUpper);
    }

    /**
     * Send a request to another process
     * 
     * @param isUnique Expect single output (true) or multiple (false)
     * @param optionalOutput Output is optional (true) or required (false)
     * @param data Data to send as JSON string
     * @param language Target language/runtime
     * @param file Path to target file
     *
     * Sets this.response with fields:
     *     - requestStatus (boolean): Success status
     *     - data (String): Response data as JSON string (preserves type)
     *     - optionalOutput (boolean): Echo of parameter
     *     - isUnique (boolean): Echo of parameter
     *     - warnings (List<String>): Warning messages
     *     - errors (List<String>): Error messages
     */
    public void request(boolean isUnique, boolean optionalOutput, String data,
                       String language, String file) {
        try {
            key = genKey();
            List<String> command = getCommand(language, file);

            // Clear previous response
            response = new InputManagerResponse();
            response.optionalOutput = optionalOutput;
            response.isUnique = isUnique;

            // Build request JSON
            Gson gson = new Gson();
            JsonObject request = new JsonObject();
            request.addProperty("key", key);
            request.addProperty("optionalOutput", optionalOutput);
            request.addProperty("isUnique", isUnique);

            if (data != null && !data.isEmpty()) {
                try {
                    JsonElement parsed = JsonParser.parseString(data);
                    request.add("data", parsed);
                } catch (Exception e) {
                    request.add("data", JsonNull.INSTANCE);
                }
            } else {
                request.add("data", JsonNull.INSTANCE);
            }

            String requestStr = gson.toJson(request);

            // CRITICAL FIX: ProcessBuilder needs ALL command parts as separate arguments
            // Don't use command.split() - use the List directly
            ProcessBuilder pb = new ProcessBuilder(command);
            pb.redirectErrorStream(true);

            java.lang.Process process = pb.start();

            // Write request to stdin
            try (OutputStream stdin = process.getOutputStream();
                 PrintWriter writer = new PrintWriter(stdin)) {
                writer.write(requestStr);
                writer.flush();
            }

            // Read output from stdout
            StringBuilder output = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    output.append(line).append("\n");
                }
            }

            int exitCode = process.waitFor();

            // Check exit code
            if (exitCode != 0) {
                response.requestStatus = false;
                response.requestStatusSet = true;
                response.errors.add("Process exited with code " + exitCode);
                response.warnings.add("Warning: these kind of errors result from an error in the targeted script.");
                return;
            }

            // Parse responses
            List<JsonObject> responses = new ArrayList<>();
            String[] lines = output.toString().trim().split("\n");
            
            for (String line : lines) {
                if (line.trim().isEmpty()) continue;

                try {
                    JsonElement json = JsonParser.parseString(line);
                    if (!json.isJsonObject()) continue;
                    
                    JsonObject jsonObj = json.getAsJsonObject();
                    JsonElement keyItem = jsonObj.get("key");
                    
                    // Validate response has matching key or null key (for init errors)
                    // This ensures we only process responses meant for this request
                    if (keyItem != null) {
                        if (keyItem.isJsonNull() ||
                            keyItem.getAsString().equals(key)) {
                            responses.add(jsonObj);
                        }
                    }
                } catch (Exception e) {
                    // Ignore lines that aren't valid JSON (e.g., debug prints)
                }
            }

            // Process responses
            if (!responses.isEmpty()) {
                boolean failure = false;

                for (JsonObject resp : responses) {
                    JsonElement status = resp.get("request_status");
                    if (status != null && status.isJsonPrimitive() && !status.getAsBoolean()) {
                        failure = true;
                    }

                    JsonElement errors = resp.get("errors");
                    if (errors != null && errors.isJsonArray()) {
                        for (JsonElement error : errors.getAsJsonArray()) {
                            if (error.isJsonPrimitive()) {
                                response.errors.add(error.getAsString());
                            }
                        }
                    }
                }

                response.requestStatus = !failure;
                response.requestStatusSet = true;

                // Collect data
                if (isUnique && responses.size() == 1) {
                    JsonElement respData = responses.get(0).get("data");
                    if (respData != null) {
                        response.data = gson.toJson(respData);
                    }
                } else if (isUnique && responses.size() > 1) {
                    response.requestStatus = false;
                    response.data = "";
                    response.errors.add("Error: Expected 1 output (isUnique=True) but received " + responses.size() + ".");
                } else {
                    JsonArray dataArray = new JsonArray();
                    for (JsonObject resp : responses) {
                        JsonElement respData = resp.get("data");
                        if (respData != null) {
                            dataArray.add(respData);
                        }
                    }
                    response.data = gson.toJson(dataArray);
                }
            } else if (optionalOutput) {
                response.requestStatusSet = false;
                response.warnings.add("Warning: the output setting is set to optional, and the targeted program didn't gave any output.");
            } else {
                response.requestStatus = false;
                response.requestStatusSet = true;
                response.errors.add("Error: OutputManager might not be used or not correctly.");
            }

        } catch (Exception e) {
            response.requestStatus = false;
            response.requestStatusSet = true;
            response.optionalOutput = optionalOutput;
            response.isUnique = isUnique;
            response.warnings.add("Warning: targeted file not found or can't be executed, consider checking file informations and language dependencies.");
            response.errors.add("Error: " + e.getMessage());
        }
    }

    /**
     * Get the full response object
     *
     * @return The complete response with status, data, errors, warnings.
     */
    public InputManagerResponse getResponse() {
        return response;
    }

    /**
     * Get the response data if request was successful
     *
     * @return The data from the response as JSON string, or empty string if request failed.
     *         Parse the JSON string to get the actual typed value.
     */
    public String getData() {
        if (response.requestStatusSet && response.requestStatus) {
            return response.data;
        }
        return "";
    }

    /**
     * Bundle any value to JSON string for use with request()
     *
     * @param value Any JSON-serializable value
     * @return JSON string representation
     */
    public static String bundle(Object value) {
        Gson gson = new Gson();
        return gson.toJson(value);
    }
}

/**
 * OutputManager - Manages receiving requests from other processes and sending responses
 * 
 * This is a static class - all methods are static.
 * Must call init() before using.
 * 
 * Static Fields:
 *     data: The request data as JSON string (accessible after init())
 * 
 * Static Methods:
 *     init(): Initialize and read request from stdin
 *     getData(): Get the request data as JSON string
 *     output(data): Send response back via stdout
 *     cleanup(): Clean up resources
 */
class OutputManager {
    private static PrintStream originalStdout;
    private static String requestJson;
    private static String key;
    private static boolean optionalOutput;
    private static boolean isUnique;
    private static boolean requestStatus;
    private static boolean requestStatusSet;
    private static boolean uniqueState;
    private static boolean uniqueStateSet;
    private static boolean initError;
    private static List<String> errors = new ArrayList<>();
    private static List<String> warnings = new ArrayList<>();

    public static String data;

    /**
     * Initialize OutputManager and read request from stdin
     * 
     * Must be called before using output() or accessing data.
     * Suppresses stdout to prevent pollution of JSON protocol.
     */
    public static void init() throws IOException {
        // Save original stdout so we can restore it later
        originalStdout = System.out;

        // Redirect stdout to suppress all System.out.println statements
        System.setOut(new PrintStream(new OutputStream() {
            @Override
            public void write(int b) {
                // Black hole - discard all output
            }
        }));

        // Read the entire stdin (the JSON request from InputManager)
        BufferedReader reader = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = reader.readLine()) != null) {
            sb.append(line);
        }
        requestJson = sb.toString();

        // Parse JSON
        Gson gson = new Gson();
        JsonObject json = JsonParser.parseString(requestJson).getAsJsonObject();

        JsonElement keyElem = json.get("key");
        if (keyElem != null) key = keyElem.getAsString();

        JsonElement dataElem = json.get("data");
        if (dataElem != null) {
            data = gson.toJson(dataElem);
        }

        JsonElement optElem = json.get("optionalOutput");
        if (optElem != null) optionalOutput = optElem.getAsBoolean();

        JsonElement uniqElem = json.get("isUnique");
        if (uniqElem != null) isUnique = uniqElem.getAsBoolean();

        // Reset state for new request
        errors.clear();
        warnings.clear();
        initError = false;
        requestStatusSet = false;
        uniqueStateSet = false;
    }

    /**
     * Get the request data as JSON string
     *
     * @return Request data as JSON string (parse to get typed value)
     */
    public static String getData() {
        return data;
    }

    /**
     * Get the request data as integer
     *
     * @return The data as int (0 if not a number)
     */
    public static int getInt() {
        if (data == null || data.isEmpty()) return 0;
        try {
            JsonElement elem = JsonParser.parseString(data);
            if (elem.isJsonPrimitive() && elem.getAsJsonPrimitive().isNumber()) {
                return elem.getAsInt();
            }
        } catch (Exception e) {}
        return 0;
    }

    /**
     * Get the request data as double
     *
     * @return The data as double (0.0 if not a number)
     */
    public static double getDouble() {
        if (data == null || data.isEmpty()) return 0.0;
        try {
            JsonElement elem = JsonParser.parseString(data);
            if (elem.isJsonPrimitive() && elem.getAsJsonPrimitive().isNumber()) {
                return elem.getAsDouble();
            }
        } catch (Exception e) {}
        return 0.0;
    }

    /**
     * Get the request data as string
     *
     * @return The data as String (empty string if not a string)
     */
    public static String getString() {
        if (data == null || data.isEmpty()) return "";
        try {
            JsonElement elem = JsonParser.parseString(data);
            if (elem.isJsonPrimitive() && elem.getAsJsonPrimitive().isString()) {
                return elem.getAsString();
            }
        } catch (Exception e) {}
        return "";
    }

    /**
     * Get the request data as boolean
     *
     * @return The data as boolean (false if not a boolean)
     */
    public static boolean getBool() {
        if (data == null || data.isEmpty()) return false;
        try {
            JsonElement elem = JsonParser.parseString(data);
            if (elem.isJsonPrimitive() && elem.getAsJsonPrimitive().isBoolean()) {
                return elem.getAsBoolean();
            }
        } catch (Exception e) {}
        return false;
    }

    /**
     * Bundle any value to JSON string for use with output()
     *
     * @param value Any JSON-serializable value
     * @return JSON string representation
     */
    public static String bundle(Object value) {
        Gson gson = new Gson();
        return gson.toJson(value);
    }

    /**
     * Send response back to the calling process
     * 
     * @param data Data to send as JSON string
     * 
     * Note:
     *     Can be called multiple times if isUnique=false in request.
     *     Will error if called multiple times when isUnique=true.
     */
    public static void output(String data) {
        Gson gson = new Gson();

        // Check if OutputManager was initialized
        if (data == null || data.isEmpty()) {
            if (!initError) {
                // Restore original stdout to actually write the response
                System.setOut(originalStdout);

                requestStatus = false;
                errors.add("Error: OutputManager isn't initialized.");

                // Build and write JSON response
                JsonObject response = new JsonObject();
                response.add("key", JsonNull.INSTANCE);
                response.addProperty("request_status", false);
                response.add("data", JsonNull.INSTANCE);
                response.addProperty("optionalOutput", optionalOutput);
                response.add("isUnique", JsonNull.INSTANCE);

                JsonArray errorsArray = new JsonArray();
                for (String err : errors) {
                    errorsArray.add(err);
                }
                response.add("errors", errorsArray);

                JsonArray warningsArray = new JsonArray();
                response.add("warnings", warningsArray);

                System.out.println(gson.toJson(response));
                System.out.flush();

                initError = true;
            }
            return;
        }

        // Check if we can output based on isUnique setting
        // uniqueStateSet tracks if we've already output once
        if (!uniqueStateSet || !isUnique) {
            requestStatus = true;

            // Restore original stdout to actually write the response
            System.setOut(originalStdout);

            // Build and write JSON response
            JsonObject response = new JsonObject();
            response.addProperty("key", key);
            response.addProperty("request_status", true);
            response.addProperty("optionalOutput", optionalOutput);
            response.addProperty("isUnique", isUnique);

            try {
                JsonElement parsed = JsonParser.parseString(data);
                response.add("data", parsed);
            } catch (Exception e) {
                response.add("data", JsonNull.INSTANCE);
            }

            response.add("errors", new JsonArray());
            response.add("warnings", new JsonArray());

            System.out.println(gson.toJson(response));
            System.out.flush();

        } else {
            // Multiple outputs when isUnique=true is an error
            requestStatus = false;
            errors.add("Error: outputs out of bound (isUnique: " + uniqueState + ").");

            // Restore original stdout
            System.setOut(originalStdout);

            JsonObject response = new JsonObject();
            response.addProperty("key", key);
            response.addProperty("request_status", false);
            response.addProperty("optionalOutput", optionalOutput);
            response.addProperty("isUnique", isUnique);

            try {
                JsonElement parsed = JsonParser.parseString(data);
                response.add("data", parsed);
            } catch (Exception e) {
                response.add("data", JsonNull.INSTANCE);
            }

            JsonArray errorsArray = new JsonArray();
            for (String err : errors) {
                errorsArray.add(err);
            }
            response.add("errors", errorsArray);

            response.add("warnings", new JsonArray());

            System.out.println(gson.toJson(response));
            System.out.flush();
        }

        // Mark that we've output once
        uniqueState = isUnique;
        uniqueStateSet = true;

        // Re-suppress stdout after writing response
        System.setOut(new PrintStream(new OutputStream() {
            @Override
            public void write(int b) {
                // Black hole
            }
        }));
    }

    /**
     * Clean up OutputManager resources
     */
    public static void cleanup() {
        errors.clear();
        warnings.clear();
    }
}