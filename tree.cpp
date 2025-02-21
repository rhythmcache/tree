// Experimental
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <memory>
#include <map>
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
    int max_depth = 999999;
    std::string target_dir = ".";
};

class TreePrinter {
private:
    struct FileStats {
        size_t directories = 0;
        size_t files = 0;
        size_t symlinks = 0;
        uintmax_t total_size = 0;
    };

    // ANSI Color codes
    const std::string COLOR_RESET = "\033[0m";
    const std::string COLOR_DIR = "\033[1;34m";    // blue
    const std::string COLOR_FILE = "\033[0;37m";   // white
    const std::string COLOR_SYMLINK = "\033[1;36m"; // cyan
    const std::string COLOR_EXEC = "\033[1;32m";    // green

    // Tree characters for different platforms
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

    void setup_console() {
        #ifdef _WIN32
        // Set console output to UTF-8
        SetConsoleOutputCP(CP_UTF8);
        
        // Enable ANSI escape sequences
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
            
            // Owner permissions
            if ((perms & fs::perms::owner_read) != fs::perms::none) result[0] = 'r';
            if ((perms & fs::perms::owner_write) != fs::perms::none) result[1] = 'w';
            if ((perms & fs::perms::owner_exec) != fs::perms::none) result[2] = 'x';
            
            // Group permissions
            if ((perms & fs::perms::group_read) != fs::perms::none) result[3] = 'r';
            if ((perms & fs::perms::group_write) != fs::perms::none) result[4] = 'w';
            if ((perms & fs::perms::group_exec) != fs::perms::none) result[5] = 'x';
            
            // Others permissions
            if ((perms & fs::perms::others_read) != fs::perms::none) result[6] = 'r';
            if ((perms & fs::perms::others_write) != fs::perms::none) result[7] = 'w';
            if ((perms & fs::perms::others_exec) != fs::perms::none) result[8] = 'x';
            
            return result;
        } catch (const std::exception&) {
            return result;
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

    void print_entry(const fs::path& path, const std::string& prefix, bool is_last) {
        std::string name = path.filename().string();
        if (name.empty()) return;

        std::string connector = is_last ? chars.corner + chars.horizontal 
                                      : chars.junction + chars.horizontal;

        std::string display_name = name;
        std::string color = COLOR_FILE;
        std::string suffix;

        try {
            if (fs::is_symlink(path)) {
                color = COLOR_SYMLINK;
                fs::path target = fs::read_symlink(path);
                suffix = " -> " + target.string();
                stats.symlinks++;
            }
            else if (fs::is_directory(path)) {
                color = COLOR_DIR;
                stats.directories++;
            }
            else {
                stats.files++;
                auto perms = fs::status(path).permissions();
                if ((perms & fs::perms::owner_exec) != fs::perms::none) {
                    color = COLOR_EXEC;
                }
            }

            if (opts.show_perms) {
                display_name = get_permissions(path) + " " + display_name;
            }

            if (opts.show_size && !fs::is_directory(path)) {
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
            print_entry(entries[i], prefix, is_last);

            if (fs::is_directory(entries[i])) {
                std::string new_prefix = prefix + (is_last ? "    " : chars.vertical + "   ");
                print_tree(entries[i], new_prefix, depth + 1);
            }
        }
    }

public:
    TreePrinter(const Options& options) 
        : opts(options), 
          chars(options.use_ascii ? ascii_chars : unicode_chars) {
        setup_console();
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

            // Print summary
            std::cout << "\n" << stats.directories << " directories";
            if (!opts.dirs_only) {
                std::cout << ", " << stats.files << " files";
            }
            if (stats.symlinks > 0) {
                std::cout << ", " << stats.symlinks << " symlinks";
            }
            if (opts.show_size) {
                std::cout << "\nTotal size: " << format_size(stats.total_size);
            }
            std::cout << std::endl;

        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            exit(1);
        }
    }
};

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS] [DIR]\n"
              << "Options:\n"
              << "  -a    Show hidden files\n"
              << "  -d    Show directories only\n"
              << "  -n    No colors\n"
              << "  -i    Use ASCII characters instead of Unicode\n"
              << "  -s    Show file sizes\n"
              << "  -p    Show permissions\n"
              << "  -L    Follow symbolic links\n"
              << "  -D n  Max display depth\n";
}

int main(int argc, char* argv[]) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] == '-') {
            for (size_t j = 1; j < arg.length(); ++j) {
                switch (arg[j]) {
                    case 'a': opts.show_all = true; break;
                    case 'd': opts.dirs_only = true; break;
                    case 'n': opts.no_color = true; break;
                    case 'i': opts.use_ascii = true; break;
                    case 's': opts.show_size = true; break;
                    case 'p': opts.show_perms = true; break;
                    case 'L': opts.follow_symlinks = true; break;
                    case 'D':
                        if (++i < argc) {
                            opts.max_depth = std::stoi(argv[i]);
                        } else {
                            std::cerr << "Error: -D requires a number\n";
                            return 1;
                        }
                        break;
                    default:
                        print_usage(argv[0]);
                        return 1;
                }
            }
        } else {
            opts.target_dir = arg;
        }
    }

    TreePrinter printer(opts);
    printer.print();
    return 0;
}
