// Experimental
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <memory>
#include <map>
#include <regex>
#ifdef _WIN32
#include <windows.h>
#endif
namespace fs = std::filesystem;

struct Options {
    bool show_all = false;
    bool dirs_only = false;
    bool no_color = false;
    bool use_ascii = false;
    bool show_size = false;
    bool show_perms = false;
    bool follow_symlinks = false;
    bool only_symlinks = false;    // only show symlinks
    bool only_executables = false; // only show executables
    bool pattern_match = false;    // filter by pattern
    bool exact_match = false;      // exact match for pattern (new flag)
    std::string pattern = "";      // match
    bool size_filter = false;      // filter by file size
    uintmax_t min_size = 0;        // minimum file size
    uintmax_t max_size = UINTMAX_MAX; // maximum file size
    int max_depth = 999999;
    std::string target_dir = ".";
};

class TreePrinter {
private:
    struct FileStats {
        size_t directories = 0;
        size_t files = 0;
        size_t symlinks = 0;
        size_t executables = 0;
        uintmax_t total_size = 0;
        size_t size_filtered_files = 0; // count of files within size range
    };

    const std::string COLOR_RESET = "\033[0m";
    const std::string COLOR_DIR = "\033[1;34m";    // blue
    const std::string COLOR_FILE = "\033[0;37m";   // white
    const std::string COLOR_SYMLINK = "\033[1;36m"; // idk
    const std::string COLOR_EXEC = "\033[1;32m";    // green

    struct TreeChars {
        std::string vertical;
        std::string horizontal;
        std::string corner;
        std::string junction;
    };

    #ifdef _WIN32
    TreeChars unicode_chars = {"│", "──", "└", "├"};
    TreeChars ascii_chars = {"|", "--", "`", "+"};
    #else
    TreeChars unicode_chars = {"│", "──", "└", "├"};
    TreeChars ascii_chars = {"|", "--", "`", "|"};
    #endif

    Options opts;
    FileStats stats;
    const TreeChars& chars;
    std::vector<fs::path> matching_paths;
    std::vector<fs::path> size_matching_paths;

    void setup_console() {
        #ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
        #endif
    }

    std::string get_permissions(const fs::path& path) {
        std::string result = "---------";
        try {
            auto perms = fs::status(path).permissions();
            
            if ((perms & fs::perms::owner_read) != fs::perms::none) result[0] = 'r';
            if ((perms & fs::perms::owner_write) != fs::perms::none) result[1] = 'w';
            if ((perms & fs::perms::owner_exec) != fs::perms::none) result[2] = 'x';
            
            if ((perms & fs::perms::group_read) != fs::perms::none) result[3] = 'r';
            if ((perms & fs::perms::group_write) != fs::perms::none) result[4] = 'w';
            if ((perms & fs::perms::group_exec) != fs::perms::none) result[5] = 'x';
            
            if ((perms & fs::perms::others_read) != fs::perms::none) result[6] = 'r';
            if ((perms & fs::perms::others_write) != fs::perms::none) result[7] = 'w';
            if ((perms & fs::perms::others_exec) != fs::perms::none) result[8] = 'x';
            
            return result;
        } catch (const std::exception&) {
            return result;
        }
    }

    bool is_executable(const fs::path& path) {
        try {
            auto perms = fs::status(path).permissions();
            return (perms & fs::perms::owner_exec) != fs::perms::none;
        } catch (const std::exception&) {
            return false;
        }
    }

    std::string format_size(uintmax_t size) {
        const char* units[] = {"B", "K", "M", "G", "T"};
        int unit_index = 0;
        double size_f = static_cast<double>(size);
        
        while (size_f >= 1024.0 && unit_index < 4) {
            size_f /= 1024.0;
            unit_index++;
        }
        
        char buffer[32];
        if (unit_index == 0) {
            snprintf(buffer, sizeof(buffer), "%5ju%s", size, units[unit_index]);
        } else {
            snprintf(buffer, sizeof(buffer), "%5.1f%s", size_f, units[unit_index]);
        }
        return std::string(buffer);
    }

    bool is_size_in_range(const fs::path& path) {
        if (!opts.size_filter) return true;
        
        try {
            if (fs::is_directory(path)) return true;
            
            uintmax_t file_size = fs::file_size(path);
            return (file_size >= opts.min_size && file_size <= opts.max_size);
        } catch (const fs::filesystem_error&) {
            return false;
        }
    }

    bool matches_pattern(const fs::path& path) {
        if (!opts.pattern_match) return true;
        
        std::string filename = path.filename().string();
        if (opts.exact_match) {
            return filename == opts.pattern;
        }
        
        if (opts.pattern.find('*') != std::string::npos || opts.pattern.find('?') != std::string::npos) {
            std::string regex_pattern = opts.pattern;
            std::string::size_type pos = 0;
            while ((pos = regex_pattern.find("*", pos)) != std::string::npos) {
                regex_pattern.replace(pos, 1, ".*");
                pos += 2;
            }
            pos = 0;
            while ((pos = regex_pattern.find("?", pos)) != std::string::npos) {
                regex_pattern.replace(pos, 1, ".");
                pos += 1;
            }
            
            std::regex pattern_regex(regex_pattern, std::regex_constants::icase);
            return std::regex_match(filename, pattern_regex);
        } else {
            return filename.find(opts.pattern) != std::string::npos;
        }
    }

    bool contains_size_matching_files(const fs::path& dir_path) {
        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (!fs::is_directory(entry.path()) && is_size_in_range(entry.path())) {
                    size_matching_paths.push_back(entry.path());
                    return true;
                }
            }
        } catch (const fs::filesystem_error&) {
        }
        return false;
    }

    bool is_on_path_to_size_match(const fs::path& path) {
        if (!opts.size_filter) return true;
        
        for (const auto& matching_path : size_matching_paths) {
            if (matching_path.string().find(path.string()) == 0) {
                return true;
            }
        }
        return false;
    }

    bool contains_matching_files(const fs::path& dir_path) {
        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (matches_pattern(entry.path())) {
                    matching_paths.push_back(entry.path());
                    return true;
                }
            }
        } catch (const fs::filesystem_error&) {
        
        }
        return false;
    }


    bool is_on_path_to_match(const fs::path& path) {
        if (!opts.pattern_match) return true;
        
        for (const auto& matching_path : matching_paths) {
            if (matching_path.string().find(path.string()) == 0) {
                return true;
            }
        }
        return false;
    }

    void print_entry(const fs::path& path, const std::string& prefix, bool is_last) {
        std::string name = path.filename().string();
        if (name.empty()) return;

        bool is_symlink = false;
        bool is_dir = false;
        bool is_exec = false;
        
        try {
            is_symlink = fs::is_symlink(path);
            is_dir = fs::is_directory(path);
            is_exec = is_executable(path);
            
            if (opts.only_symlinks && !is_symlink) return;
            if (opts.only_executables && !is_exec && !is_dir) return;
            if (opts.pattern_match && !is_dir && !matches_pattern(path)) return;
            if (opts.pattern_match && is_dir && !is_on_path_to_match(path)) return;
            
            if (opts.size_filter && !is_dir && !is_size_in_range(path)) return;
            if (opts.size_filter && is_dir && !is_on_path_to_size_match(path)) return;
            
        } catch (const fs::filesystem_error&) {
            if (opts.only_symlinks || opts.only_executables || 
                opts.pattern_match || opts.size_filter) return;
        }

        std::string connector = is_last ? chars.corner + chars.horizontal 
                                      : chars.junction + chars.horizontal;

        std::string display_name = name;
        std::string color = COLOR_FILE;
        std::string suffix;

        try {
            if (is_symlink) {
                color = COLOR_SYMLINK;
                fs::path target = fs::read_symlink(path);
                suffix = " -> " + target.string();
                stats.symlinks++;
            }
            else if (is_dir) {
                color = COLOR_DIR;
                stats.directories++;
            }
            else {
                stats.files++;
                
                if (opts.size_filter && is_size_in_range(path)) {
                    stats.size_filtered_files++;
                }
                
                if (is_exec) {
                    color = COLOR_EXEC;
                    stats.executables++;
                }
            }

            if (opts.show_perms) {
                display_name = get_permissions(path) + " " + display_name;
            }

            if ((opts.show_size || opts.size_filter) && !is_dir) {
                uintmax_t size = fs::file_size(path);
                stats.total_size += size;
                display_name = format_size(size) + " " + display_name;
            }

        } catch (const fs::filesystem_error&) {
            display_name = "[error accessing " + name + "]";
            color = "\033[1;31m"; // red
        }

        if (opts.no_color) {
            std::cout << prefix << connector << " " << display_name << suffix << std::endl;
        } else {
            std::cout << prefix << connector << " " << color << display_name 
                     << COLOR_RESET << suffix << std::endl;
        }
    }

    void print_tree(const fs::path& dir_path, const std::string& prefix, int depth) {
        if (depth > opts.max_depth) return;

        std::vector<fs::path> entries;
        try {
            for (const auto& entry : fs::directory_iterator(dir_path)) {
                if (!opts.show_all && entry.path().filename().string()[0] == '.') {
                    continue;
                }
                
                if (opts.dirs_only && !fs::is_directory(entry.path())) {
                    continue;
                }
                
                if (opts.only_symlinks && !fs::is_symlink(entry.path())) {
                    if (!fs::is_directory(entry.path())) continue;
                }
                
                if (opts.only_executables && !is_executable(entry.path())) {
                    if (!fs::is_directory(entry.path())) continue;
                }
                
                entries.push_back(entry.path());
            }
        } catch (const fs::filesystem_error&) {
            std::cerr << "Error: Permission denied or other error accessing " 
                     << dir_path << std::endl;
            return;
        }

        std::sort(entries.begin(), entries.end());

        for (size_t i = 0; i < entries.size(); ++i) {
            bool is_last = (i == entries.size() - 1);
            bool is_dir = fs::is_directory(entries[i]);
            
            bool show_entry = true;
            
            if (opts.pattern_match) {
                if (is_dir) {
                    show_entry = is_on_path_to_match(entries[i]);
                } else {
                    show_entry = matches_pattern(entries[i]);
                }
            }
            
            if (show_entry && opts.size_filter) {
                if (is_dir) {
                    show_entry = is_on_path_to_size_match(entries[i]);
                } else {
                    show_entry = is_size_in_range(entries[i]);
                }
            }
            
            if (show_entry) {
                print_entry(entries[i], prefix, is_last);
                
                if (is_dir) {
                    std::string new_prefix = prefix + (is_last ? "    " : chars.vertical + "   ");
                    print_tree(entries[i], new_prefix, depth + 1);
                }
            }
        }
    }

public:
    TreePrinter(const Options& options) 
        : opts(options), 
          chars(options.use_ascii ? ascii_chars : unicode_chars) {
        setup_console();
        
        if (opts.pattern_match) {
            try {
                contains_matching_files(fs::path(opts.target_dir));
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Error during pattern pre-scan: " << e.what() << std::endl;
            }
        }
        
        if (opts.size_filter) {
            try {
                contains_size_matching_files(fs::path(opts.target_dir));
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Error during size filter pre-scan: " << e.what() << std::endl;
            }
        }
    }

    void print() {
        fs::path root_path = fs::path(opts.target_dir);
        
        try {
            if (fs::is_symlink(root_path)) {
                std::cout << opts.target_dir << " -> " 
                         << fs::read_symlink(root_path).string() << std::endl;
            } else {
                std::cout << opts.target_dir << std::endl;
            }

            print_tree(root_path, "", 1);

            std::cout << "\n" << stats.directories << " directories";
            if (!opts.dirs_only) {
                std::cout << ", " << stats.files << " files";
            }
            if (stats.symlinks > 0) {
                std::cout << ", " << stats.symlinks << " symlinks";
            }
            if (stats.executables > 0) {
                std::cout << ", " << stats.executables << " executables";
            }
            if (opts.size_filter) {
                std::cout << ", " << stats.size_filtered_files << " size-filtered files";
                std::cout << " (" << format_size(opts.min_size) << " - " 
                         << format_size(opts.max_size) << ")";
            }
            if (opts.show_size || opts.size_filter) {
                std::cout << "\nTotal size: " << format_size(stats.total_size);
            }
            std::cout << std::endl;

        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            exit(1);
        }
    }
};

uintmax_t parse_size(const std::string& size_str) {
    if (size_str.empty()) return 0;
    
    double value = 0;
    char unit = 'B';
    
    std::string numeric_part = size_str;
    if (!isdigit(size_str.back())) {
        unit = toupper(size_str.back());
        numeric_part = size_str.substr(0, size_str.length() - 1);
    }
    
    try {
        value = std::stod(numeric_part);
    } catch (const std::exception&) {
        std::cerr << "Error parsing size: " << size_str << std::endl;
        return 0;
    }
    
    switch (unit) {
        case 'K': return static_cast<uintmax_t>(value * 1024);
        case 'M': return static_cast<uintmax_t>(value * 1024 * 1024);
        case 'G': return static_cast<uintmax_t>(value * 1024 * 1024 * 1024);
        case 'T': return static_cast<uintmax_t>(value * 1024ULL * 1024ULL * 1024ULL * 1024ULL);
        default: return static_cast<uintmax_t>(value); // Bytes
    }
}

bool parse_size_range(const std::string& range_str, uintmax_t& min_size, uintmax_t& max_size) {
    min_size = 0;
    max_size = UINTMAX_MAX;
    
    size_t colon_pos = range_str.find(':');
    if (colon_pos == std::string::npos) {
        std::string min_str = range_str;
        if (!min_str.empty()) {
            min_size = parse_size(min_str);
        }
    } else {
        std::string min_str = range_str.substr(0, colon_pos);
        std::string max_str = range_str.substr(colon_pos + 1);
        
        if (!min_str.empty()) {
            min_size = parse_size(min_str);
        }
        if (!max_str.empty()) {
            max_size = parse_size(max_str); 
        }
    }
    
    return true;
}

void print_usage() {
    std::cerr << "Usage: tree [OPTIONS] [DIR]\n"
              << "Options:\n"
              << "  -a                     Show hidden files\n"
              << "  -d                     Show directories only\n"
              << "  -n                     No colors\n"
              << "  -i                     Use ASCII characters\n"
              << "  -s                     Show file sizes\n"
              << "  -p                     Show permissions\n"
              << "  -L                     Follow symbolic links\n"
              << "  -l                     Show only symbolic links\n"
              << "  -e                     Show only executable files\n"
              << "  -P <name> [--exact]    Show only files with that name\n"
              << "  -S range               Show only files within size range (e.g., 36K:1M)\n"  
              << "  -D n                   Max display depth\n";
}


int main(int argc, char* argv[]) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--exact") {
            opts.exact_match = true;
            continue;
        }
        
        if (arg[0] == '-' && arg.length() > 1 && arg[1] != '-') {
            for (size_t j = 1; j < arg.length(); ++j) {
                switch (arg[j]) {
                    case 'a': opts.show_all = true; break;
                    case 'd': opts.dirs_only = true; break;
                    case 'n': opts.no_color = true; break;
                    case 'i': opts.use_ascii = true; break;
                    case 's': opts.show_size = true; break;
                    case 'p': opts.show_perms = true; break;
                    case 'L': opts.follow_symlinks = true; break;
                    case 'l': opts.only_symlinks = true; break;
                    case 'e': opts.only_executables = true; break;
                    case 'P':
                        if (++i < argc) {
                            opts.pattern_match = true;
                            opts.pattern = argv[i];
                        } else {
                            std::cerr << "Error: -P requires a pattern\n";
                            return 1;
                        }
                        break;
                    case 'S': 
                        if (++i < argc) {
                            opts.size_filter = true;
                            if (!parse_size_range(argv[i], opts.min_size, opts.max_size)) {
                                std::cerr << "Error: Invalid size range format. Use '36K:1M' format.\n";
                                return 1;
                            }
                        } else {
                            std::cerr << "Error: -S requires a size range\n";
                            return 1;
                        }
                        break;
                    case 'D':
                        if (++i < argc) {
                            opts.max_depth = std::stoi(argv[i]);
                        } else {
                            std::cerr << "Error: -D requires a number\n";
                            return 1;
                        }
                        break;
                    case 'f':  // same as -P
                        if (++i < argc) {
                            opts.pattern_match = true;
                            opts.pattern = argv[i];
                        } else {
                            std::cerr << "Error: -f requires a pattern\n";
                            return 1;
                        }
                        break;
                    default:
                        print_usage();
                        return 1;
                }
            }
        } else if (arg[0] != '-') {
            opts.target_dir = arg;
        }
    }

    TreePrinter printer(opts);
    printer.print();
    return 0;
}
