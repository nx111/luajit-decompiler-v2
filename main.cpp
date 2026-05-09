#include "main.h"

#ifdef _WIN32
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <cerrno>
#include <climits>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct Error {
	const std::string message;
	const std::string filePath;
	const std::string function;
	const std::string source;
	const std::string line;
};

static bool isProgressBarActive = false;
static uint32_t filesSkipped = 0;

static struct {
	bool showHelp = false;
	bool silentAssertions = false;
	bool forceOverwrite = false;
	bool ignoreDebugInfo = false;
	bool minimizeDiffs = false;
	bool unrestrictedAscii = false;
	std::string inputPath;
	std::string outputPath;
	std::string extensionFilter;
} arguments;

static bool is_path_separator(const char& character) {
#ifdef _WIN32
	return character == '/' || character == '\\';
#else
	return character == '/';
#endif
}

static std::string string_to_lowercase(const std::string& string) {
	std::string lowercaseString = string;

	for (uint32_t i = lowercaseString.size(); i--;) {
		if (lowercaseString[i] < 'A' || lowercaseString[i] > 'Z') continue;
		lowercaseString[i] += 'a' - 'A';
	}

	return lowercaseString;
}

static std::string join_path(const std::string& left, const std::string& right) {
	if (!left.size()) return right;
	if (!right.size()) return left;
	if (is_path_separator(left.back())) return left + right;
#ifdef _WIN32
	return left + "\\" + right;
#else
	return left + "/" + right;
#endif
}

static std::string parent_path(const std::string& path) {
	const size_t separator = path.find_last_of("/\\");
	if (separator == std::string::npos) return "";
	if (!separator) return path.substr(0, 1);
	return path.substr(0, separator);
}

static std::string filename(const std::string& path) {
	const size_t separator = path.find_last_of("/\\");
	return separator == std::string::npos ? path : path.substr(separator + 1);
}

static std::string extension(const std::string& path) {
	const std::string name = filename(path);
	const size_t dot = name.find_last_of('.');
	return dot == std::string::npos || !dot ? "" : name.substr(dot);
}

static std::string replace_extension_with_lua(const std::string& path) {
	const size_t separator = path.find_last_of("/\\");
	const size_t nameBegin = separator == std::string::npos ? 0 : separator + 1;
	const size_t dot = path.find_last_of('.');
	if (dot == std::string::npos || dot < nameBegin) return path + ".lua";
	return path.substr(0, dot) + ".lua";
}

static bool path_exists(const std::string& path) {
#ifdef _WIN32
	return fs::exists(path);
#else
	struct stat pathStat;
	return !stat(path.c_str(), &pathStat);
#endif
}

static bool is_directory(const std::string& path) {
#ifdef _WIN32
	return fs::is_directory(path);
#else
	struct stat pathStat;
	return !stat(path.c_str(), &pathStat) && S_ISDIR(pathStat.st_mode);
#endif
}

static bool is_regular_file(const std::string& path) {
#ifdef _WIN32
	return fs::is_regular_file(path);
#else
	struct stat pathStat;
	return !stat(path.c_str(), &pathStat) && S_ISREG(pathStat.st_mode);
#endif
}

static std::string current_path() {
#ifdef _WIN32
	return fs::current_path().string();
#else
	char buffer[PATH_MAX];
	return getcwd(buffer, sizeof(buffer)) ? std::string(buffer) : ".";
#endif
}

static bool create_directories(const std::string& path) {
	if (!path.size()) return true;
#ifdef _WIN32
	return fs::create_directories(path) || fs::is_directory(path);
#else
	std::string currentPath;
	size_t componentBegin = 0;

	if (is_path_separator(path.front())) {
		currentPath = "/";
		componentBegin = 1;
	}

	while (componentBegin < path.size()) {
		while (componentBegin < path.size() && is_path_separator(path[componentBegin])) componentBegin++;
		size_t componentEnd = componentBegin;
		while (componentEnd < path.size() && !is_path_separator(path[componentEnd])) componentEnd++;
		if (componentEnd == componentBegin) break;

		if (currentPath.size() && !is_path_separator(currentPath.back())) currentPath += '/';
		currentPath += path.substr(componentBegin, componentEnd - componentBegin);

		if (!is_directory(currentPath) && mkdir(currentPath.c_str(), 0777) && errno != EEXIST) return false;
		componentBegin = componentEnd;
	}

	return is_directory(path);
#endif
}

static bool file_matches_extension_filter(const std::string& filePath) {
	return !arguments.extensionFilter.size() || arguments.extensionFilter == string_to_lowercase(extension(filePath));
}

#ifdef _WIN32
static std::vector<std::string> find_files_recursively() {
	std::vector<std::string> files;

	for (const fs::directory_entry& entry : fs::recursive_directory_iterator(arguments.inputPath)) {
		if (!entry.is_regular_file() || !file_matches_extension_filter(entry.path().string())) continue;
		files.emplace_back(fs::relative(entry.path(), arguments.inputPath).string());
	}

	std::sort(files.begin(), files.end());
	return files;
}
#else
static void find_files_recursively(const std::string& relativePath, std::vector<std::string>& files) {
	const std::string directoryPath = relativePath.size() ? join_path(arguments.inputPath, relativePath) : arguments.inputPath;
	DIR* const directory = opendir(directoryPath.c_str());
	if (!directory) return;

	while (dirent* const entry = readdir(directory)) {
		if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;

		const std::string childRelativePath = relativePath.size() ? join_path(relativePath, entry->d_name) : entry->d_name;
		const std::string childPath = join_path(arguments.inputPath, childRelativePath);

		if (is_directory(childPath)) {
			find_files_recursively(childRelativePath, files);
		} else if (is_regular_file(childPath) && file_matches_extension_filter(childPath)) {
			files.emplace_back(childRelativePath);
		}
	}

	closedir(directory);
}

static std::vector<std::string> find_files_recursively() {
	std::vector<std::string> files;
	find_files_recursively("", files);
	std::sort(files.begin(), files.end());
	return files;
}
#endif

static bool decompile_file(const std::string& relativePath) {
	const std::string inputFile = join_path(arguments.inputPath, relativePath);
	const std::string outputFile = join_path(arguments.outputPath, replace_extension_with_lua(relativePath));

	if (!create_directories(parent_path(outputFile))) {
		print("Failed to create output path: " + parent_path(outputFile));
		return false;
	}

	Bytecode bytecode(inputFile);
	Ast ast(bytecode, arguments.ignoreDebugInfo, arguments.minimizeDiffs);
	Lua lua(bytecode, ast, outputFile, arguments.forceOverwrite, arguments.minimizeDiffs, arguments.unrestrictedAscii);

	try {
		print("--------------------\nInput file: " + bytecode.filePath + "\nReading bytecode...");
		bytecode();
		print("Building ast...");
		ast();
		print("Writing lua source...");
		lua();
		print("Output file: " + lua.filePath);
	} catch (const Error& error) {
		erase_progress_bar();
		print("\nError running " + error.function + "\nSource: " + error.source + ":" + error.line + "\nFile: " + error.filePath + "\n\n" + error.message);

		if (arguments.silentAssertions) {
			filesSkipped++;
			return true;
		}

		return false;
	} catch (const std::exception& error) {
		erase_progress_bar();
		print("Unknown exception\nFile: " + bytecode.filePath + "\n\n" + error.what());
		return false;
	} catch (...) {
		erase_progress_bar();
		print("Unknown exception\nFile: " + bytecode.filePath);
		return false;
	}

	return true;
}

static bool decompile_files(const std::vector<std::string>& files) {
	for (const std::string& file : files) {
		if (!decompile_file(file)) return false;
	}

	return true;
}

static char* parse_arguments(const int& argc, char** const& argv) {
	if (argc < 2) return nullptr;
	arguments.inputPath = argv[1];
	bool isInputPathSet = true;

	if (arguments.inputPath.size() && arguments.inputPath.front() == '-') {
		arguments.inputPath.clear();
		isInputPathSet = false;
	}

	std::string argument;

	for (uint32_t i = isInputPathSet ? 2 : 1; i < argc; i++) {
		argument = argv[i];

		if (argument.size() >= 2 && argument.front() == '-') {
			if (argument[1] == '-') {
				argument = argument.c_str() + 2;

				if (argument == "extension") {
					if (i <= argc - 2) {
						i++;
						arguments.extensionFilter = argv[i];
						continue;
					}
				} else if (argument == "force_overwrite") {
					arguments.forceOverwrite = true;
					continue;
				} else if (argument == "help") {
					arguments.showHelp = true;
					continue;
				} else if (argument == "ignore_debug_info") {
					arguments.ignoreDebugInfo = true;
					continue;
				} else if (argument == "minimize_diffs") {
					arguments.minimizeDiffs = true;
					continue;
				} else if (argument == "output") {
					if (i <= argc - 2) {
						i++;
						arguments.outputPath = argv[i];
						continue;
					}
				} else if (argument == "silent_assertions") {
					arguments.silentAssertions = true;
					continue;
				} else if (argument == "unrestricted_ascii") {
					arguments.unrestrictedAscii = true;
					continue;
				}
			} else if (argument.size() == 2) {
				switch (argument[1]) {
				case 'e':
					if (i > argc - 2) break;
					i++;
					arguments.extensionFilter = argv[i];
					continue;
				case 'f':
					arguments.forceOverwrite = true;
					continue;
				case '?':
				case 'h':
					arguments.showHelp = true;
					continue;
				case 'i':
					arguments.ignoreDebugInfo = true;
					continue;
				case 'm':
					arguments.minimizeDiffs = true;
					continue;
				case 'o':
					if (i > argc - 2) break;
					i++;
					arguments.outputPath = argv[i];
					continue;
				case 's':
					arguments.silentAssertions = true;
					continue;
				case 'u':
					arguments.unrestrictedAscii = true;
					continue;
				}
			}
		}

		return argv[i];
	}

	return nullptr;
}

static bool prepare_output_path() {
	if (!arguments.outputPath.size()) arguments.outputPath = join_path(current_path(), "output");

	if (!create_directories(arguments.outputPath)) {
		print("Failed to create output path: " + arguments.outputPath);
		return false;
	}

	if (!is_directory(arguments.outputPath)) {
		print("Output path is not a folder: " + arguments.outputPath);
		return false;
	}

	return true;
}

static bool prepare_extension_filter() {
	if (!arguments.extensionFilter.size()) return true;
	if (arguments.extensionFilter.front() != '.') arguments.extensionFilter.insert(arguments.extensionFilter.begin(), '.');
	arguments.extensionFilter = string_to_lowercase(arguments.extensionFilter);
	return true;
}

int main(int argc, char* argv[]) {
	print(std::string(PROGRAM_NAME) + "\nCompiled on " + __DATE__);
	
	char* invalidArgument = parse_arguments(argc, argv);
	if (invalidArgument) {
		print("Invalid argument: " + std::string(invalidArgument) + "\nUse -? to show usage and options.");
		return EXIT_FAILURE;
	}
	
	if (arguments.showHelp) {
		print(
			"Usage: luajit-decompiler-v2 INPUT_PATH [options]\n"
			"\n"
			"Available options:\n"
			"  -h, -?, --help\t\tShow this message\n"
			"  -o, --output OUTPUT_PATH\tOverride default output directory\n"
			"  -e, --extension EXTENSION\tOnly decompile files with the specified extension\n"
			"  -s, --silent_assertions\tDisable assertion error prompt\n"
			"\t\t\t\t  and auto skip files that fail to decompile\n"
			"  -f, --force_overwrite\t\tAlways overwrite existing files\n"
			"  -i, --ignore_debug_info\tIgnore bytecode debug info\n"
			"  -m, --minimize_diffs\t\tOptimize output formatting to help minimize diffs\n"
			"  -u, --unrestricted_ascii\tDisable default UTF-8 encoding and string restrictions"
		);
		return EXIT_SUCCESS;
	}
	
	if (!arguments.inputPath.size()) {
		print("No input path specified!");
		return EXIT_FAILURE;
	}

	try {
		if (!prepare_extension_filter()) return EXIT_FAILURE;

		if (!path_exists(arguments.inputPath)) {
			print("Failed to open input path: " + arguments.inputPath);
			return EXIT_FAILURE;
		}

		std::vector<std::string> files;

		if (is_directory(arguments.inputPath)) {
			files = find_files_recursively();

			if (!files.size()) {
				print("No files " + (arguments.extensionFilter.size() ? "with extension " + arguments.extensionFilter + " " : "") + "found in path: " + arguments.inputPath);
				return EXIT_FAILURE;
			}
		} else {
			if (!file_matches_extension_filter(arguments.inputPath)) {
				print("Input file does not match extension filter: " + arguments.inputPath);
				return EXIT_FAILURE;
			}

			const std::string inputFile = arguments.inputPath;
			arguments.inputPath = parent_path(inputFile).size() ? parent_path(inputFile) : ".";
			files.emplace_back(filename(inputFile));
		}

		if (!prepare_output_path()) return EXIT_FAILURE;

		if (!decompile_files(files)) {
			print("--------------------\nAborted!");
			return EXIT_FAILURE;
		}
	} catch (const std::exception& error) {
		erase_progress_bar();
		print(std::string("Error: ") + error.what());
		return EXIT_FAILURE;
	}

	print("--------------------\n" + (filesSkipped ? "Failed to decompile " + std::to_string(filesSkipped) + " file" + (filesSkipped > 1 ? "s" : "") + ".\n" : "") + "Done!");
	return EXIT_SUCCESS;
}

void print(const std::string& message) {
	std::cout << message << '\n';
}

void print_progress_bar(const double& progress, const double& total) {
	static char PROGRESS_BAR[] = "\r[====================]";

	const uint8_t threshold = std::round(20 / total * progress);

	for (uint8_t i = 20; i--;) {
		PROGRESS_BAR[i + 2] = i < threshold ? '=' : ' ';
	}

	std::cout.write(PROGRESS_BAR, sizeof(PROGRESS_BAR) - 1);
	std::cout.flush();
	isProgressBarActive = true;
}

void erase_progress_bar() {
	static constexpr char PROGRESS_BAR_ERASER[] = "\r                      \r";

	if (!isProgressBarActive) return;
	std::cout.write(PROGRESS_BAR_ERASER, sizeof(PROGRESS_BAR_ERASER) - 1);
	std::cout.flush();
	isProgressBarActive = false;
}

void assert(const bool& assertion, const std::string& message, const std::string& filePath, const std::string& function, const std::string& source, const uint32_t& line) {
	if (!assertion) throw Error{
		.message = message,
		.filePath = filePath,
		.function = function,
		.source = source,
		.line = std::to_string(line)
	};
}

std::string byte_to_string(const uint8_t& byte) {
	char string[] = "0x00";
	uint8_t digit;
	
	for (uint8_t i = 2; i--;) {
		digit = (byte >> i * 4) & 0xF;
		string[3 - i] = digit >= 0xA ? 'A' + digit - 0xA : '0' + digit;
	}

	return string;
}
