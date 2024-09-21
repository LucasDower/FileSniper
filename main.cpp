#include <ncurses.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>
#include <chrono>
#include <format>
#include <atomic>

struct file_size
{
    std::filesystem::path path;
    size_t size;
};

template <typename T>
struct thread_safe_vector
{
    void add(const T& value)
    {
        std::scoped_lock scope_lock(lock);
        vec.push_back(value);
    }

    void append(const std::vector<T>& others)
    {
        std::scoped_lock scope_lock(lock);
        vec.insert(vec.end(), others.begin(), others.end());
    }

    void resize(const size_t size)
    {
        std::scoped_lock scope_lock(lock);
        vec.resize(size);
    }

    void stable_sort(const std::function<bool(const T&, const T&)> order_func)
    {
        std::scoped_lock scope_lock(lock);
        std::stable_sort(vec.begin(), vec.end(), order_func);
    }

    std::vector<T> get_copy()
    {
        std::scoped_lock scope_lock(lock);
        return vec;
    }

    void transfer(const size_t count, std::vector<T>& destination)
    {
        std::scoped_lock scope_lock(lock);
        if (!vec.empty())
        {
            const size_t take_count = std::min<size_t>(count, vec.size());

            destination.resize(take_count);
            std::copy(vec.begin(), vec.begin() + take_count, destination.begin());

            vec.erase(vec.begin(), vec.begin() + take_count);
        }
    }

    bool is_empty()
    {
        std::scoped_lock scope_lock(lock);
        return vec.empty();
    }

private:
    std::vector<T> vec;
    std::mutex lock;
};

thread_safe_vector<std::filesystem::path> directory_queue;
thread_safe_vector<file_size> largest_files;

std::atomic<bool> stop_flag(false);
std::atomic<int> failed_reads = 0;
std::atomic<int> threads_working = 0;
std::atomic<size_t> files_scanned = 0;
std::atomic<size_t> directories_scanned = 0;

void process_directories(const std::vector<std::filesystem::path>& paths)
{
    ++threads_working;

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
        ++failed_reads;
        // Cannot read directory
    }

    directory_queue.append(directories);

    largest_files.append(file_sizes);
    largest_files.stable_sort([](const file_size& lhs, const file_size& rhs)
    {
        return lhs.size > rhs.size;
    });
    largest_files.resize(100);
    
    files_scanned += file_sizes.size();
    directories_scanned += directories.size();
    
    --threads_working;
}

void thread_job()
{
    std::vector<std::filesystem::path> directories;

    while (!stop_flag.load())
    {
        directory_queue.transfer(100, directories);
        if (!directories.empty())
        {
            process_directories(directories);
        }
        directories.clear();
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

void render_frame(const terminal_context& terminal_ctx)
{
    const std::vector<file_size>& local_largest_files = largest_files.get_copy();

    for (int i = 0; i < std::min<int>(terminal_ctx.max_y - 4, local_largest_files.size()); i++)
    {
        const auto&[path, size] = local_largest_files[i];

        mvprintw(i + 2, 5, pretty_bytes(size).c_str());
        mvprintw(i + 2, 20, path.c_str());
    }

    mvprintw(0, 0, "FILE SNIPER | %d files scanned | %d directories scanned | %d threads active | %d failed directories | %s", files_scanned.load(), directories_scanned.load(), threads_working.load(), failed_reads.load(), stop_flag.load() ? "Complete" : "Searching...");
    mvprintw(terminal_ctx.max_y - 1, 0, "(Q) - Quit");
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: filesniper <directory>\n";
        return 1;
    }

    directory_queue.add(std::filesystem::path(argv[1]));

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

        if (threads_working.load() == 0 && directory_queue.is_empty())
        {
            stop_flag = true;
        }

        // render
        {
            clear();

            terminal_context terminal_ctx;
            getmaxyx(stdscr, terminal_ctx.max_y, terminal_ctx.max_x);

            render_frame(terminal_ctx);
            refresh();
        }

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
    }

    nocbreak();
    endwin();
    
    return 0;
}