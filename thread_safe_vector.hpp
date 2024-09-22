#pragma once

template <typename T>
struct thread_safe_vector
{
    void emplace_back(T value)
    {
        std::scoped_lock scope_lock(lock);
        vec.emplace_back(std::move(value));
    }

    void insert_sorted(T value)
    {
        std::scoped_lock scope_lock(lock);

        auto it = vec.begin();
        while (it != vec.end() && *it < value) {
            ++it;
        }

        if (it - vec.begin() < 100)
        {
            vec.insert(it, value);
        }
    }

    void append(const std::vector<T>& others)
    {
        std::scoped_lock scope_lock(lock);
        vec.insert(vec.end(), others.begin(), others.end());
    }

    size_t size()
    {
        std::scoped_lock scope_lock(lock);
        return vec.size();
    }

    void resize(const size_t size)
    {
        std::scoped_lock scope_lock(lock);
        vec.resize(size);
    }

    void stable_sort(const std::function<bool(const T&, const T&)>& order_func)
    {
        std::scoped_lock scope_lock(lock);
        std::stable_sort(vec.begin(), vec.end(), order_func);
    }

    std::vector<T> get_copy()
    {
        std::scoped_lock scope_lock(lock);
        return vec;
    }

    bool pop(T& out)
    {
        std::scoped_lock scope_lock(lock);

        if (!vec.empty())
        {
            out = vec.back();
            vec.pop_back();
            return true;
        }

        return false;
    }

    bool pop_and_increment(T& out, std::atomic<int>& k)
    {
        std::scoped_lock scope_lock(lock);

        if (!vec.empty())
        {
            ++k;

            out = vec.back();
            vec.pop_back();
            return true;
        }

        return false;
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