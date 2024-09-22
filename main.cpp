#include <iostream>
#include <filesystem>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>
#include <chrono>
#include <format>
#include <atomic>
#include <functional>
#include "thread_safe_vector.hpp"

struct file_size
{
    std::filesystem::path path;
    size_t size;
};

struct file_sniper_context
{
    thread_safe_vector<std::filesystem::path> directory_queue;
    thread_safe_vector<file_size> largest_files;

    std::atomic<int> threads_working = 0;
};

struct thread_context
{
    std::vector<file_size> files;
};

void process_directory(file_sniper_context& file_sniper_ctx, thread_context& thread_ctx, const std::filesystem::path& path)
{
    ++file_sniper_ctx.threads_working;

    try
    {
        for (const auto& file : std::filesystem::directory_iterator(path))
        {
            if (file.is_regular_file())
            {
                //file_sniper_ctx.largest_files.add(file_size{ file.path(), file.file_size() });
                thread_ctx.files.emplace_back(file.path(), file.file_size());
            }
            else if (file.is_directory())
            {
                file_sniper_ctx.directory_queue.add(file.path());
                //thread_ctx.directories.emplace_back(file.path());
            }
            else
            {
                // Unknown type
            }
        }
    }
    catch (const std::exception& e)
    {
        // Cannot read directory
    }

    --file_sniper_ctx.threads_working;
}

void thread_job(file_sniper_context& file_sniper_ctx)
{
    thread_context thread_ctx;

    std::filesystem::path path;

    while (true)
    {
        if (file_sniper_ctx.directory_queue.pop(path))
        {
            process_directory(file_sniper_ctx, thread_ctx, path);
        }
        else if (file_sniper_ctx.threads_working == 0)
        {
            break;
        }
    }

    file_sniper_ctx.largest_files.append(thread_ctx.files);
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: filesniper <directory>\n";
        return 1;
    }

    const auto start = std::chrono::high_resolution_clock::now();

    file_sniper_context file_sniper_ctx;
    file_sniper_ctx.directory_queue.add(argv[1]);

    std::vector<std::thread> workers;

    const int worker_count = std::thread::hardware_concurrency();

    workers.reserve(worker_count);

    for (int i = 0; i < worker_count; ++i)
    {
        workers.emplace_back(thread_job, std::ref(file_sniper_ctx));
    }

    for (std::thread& worker : workers)
    {
        worker.join();
    }
    
    const auto finish = std::chrono::high_resolution_clock::now();
    std::cout << file_sniper_ctx.largest_files.size() << std::endl;
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count() << " ms\n";

    return 0;
}