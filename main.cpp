#include <ncurses.h>
#include <unistd.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>
#include <chrono>
#include <format>

struct file_size
{
    std::filesystem::path path;
    size_t size;
};

std::mutex lock;
size_t files_scanned = 0;
size_t directories_scanned = 0;
std::vector<std::filesystem::path> directory_queue;
std::vector<file_size> largest_files;

void process_directories(const std::vector<std::filesystem::path>& paths)
{
    std::vector<file_size> file_sizes;
    std::vector<std::filesystem::path> directories;

    try
    {
        for (const auto& path : paths)
        {
            for (const auto& file : std::filesystem::directory_iterator(path))
            {
                if (file.is_regular_file())
                {
                    file_sizes.push_back(file_size{ file.path(), file.file_size() });
                }
                else if (file.is_directory())
                {
                    directories.push_back(file.path());
                }
                else
                {
                    // Unknown type
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        // Cannot read directory
    }

    lock.lock();
    directory_queue.insert(directory_queue.end(), directories.begin(), directories.end());
    largest_files.insert(largest_files.end(), file_sizes.begin(), file_sizes.end());
    std::stable_sort(largest_files.begin(), largest_files.end(), [](const file_size& lhs, const file_size& rhs)
    {
        return lhs.size > rhs.size;
    });
    largest_files.resize(100);
    files_scanned += file_sizes.size();
    directories_scanned += directories.size();
    lock.unlock();
}

void thread_job()
{
    while (true)
    {
        lock.lock();
        if (!directory_queue.empty())
        {
            const auto first = directory_queue.begin();
            const size_t take_count = std::min<size_t>(100, directory_queue.size());
            const auto last = directory_queue.begin() + take_count;
            std::vector<std::filesystem::path> paths(first, last);
            directory_queue.erase(directory_queue.begin(), directory_queue.begin() + take_count);
            lock.unlock();

            process_directories(paths);
        }
        else
        {
            lock.unlock();
        }
    }
}

struct terminal_context
{
    int max_x;
    int max_y;

    terminal_context()
        : max_x(0)
        , max_y(0)
    {
    }
};

std::string pretty_bytes(const size_t size)
{
    if (size < 1'000)
    {
        return std::format("{} B", size);
    }
    if (size < 1'000'000)
    {
        return std::format("{:.2f} KB", static_cast<double>(size) / 1000);
    }
    if (size < 1'000'000'000)
    {
        return std::format("{:.2f} MB", static_cast<double>(size) / 1'000'000); 
    }
    return std::format("{:.2f} GB", static_cast<double>(size) / 1'000'000'000); 
}

void render_frame(const terminal_context& context)
{
    lock.lock();

    for (int i = 0; i < context.max_y - 4; i++)
    {
        const auto&[path, size] = largest_files[i];

        mvprintw(i + 2, 0, pretty_bytes(size).c_str());
        mvprintw(i + 2, 15, path.c_str());
    }

    mvprintw(0, 0, "FILE SNIPER | %d files scanned | %d directories scanned", files_scanned, directories_scanned);
    mvprintw(context.max_y - 1, 0, "(Q) - Quit");

    lock.unlock();
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: filesniper <directory>\n";
        return 1;
    }

    directory_queue.push_back(std::filesystem::path(argv[1]));

    std::vector<std::thread> workers;
    workers.reserve(std::thread::hardware_concurrency());

    for (int i = 0; i < std::thread::hardware_concurrency(); ++i)
    {
        workers.emplace_back(thread_job);
    }

    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, TRUE);

    while (true)
    {
        const int ch = getch();
        if (ch == 'q')
        {
            break;
        }

        // render
        {
            clear();

            terminal_context context;
            getmaxyx(stdscr, context.max_y, context.max_x);

            render_frame(context);
            refresh();
        }

        usleep(100'000); // 1/10 s
    }

    nocbreak();
    endwin();
    
    return 0;
}