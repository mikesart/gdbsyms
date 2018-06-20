/*
 * Copyright (c) 2018, Michael Sartain
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

//$ TODO mikesart: C++11 threads, affinity and hyperthreading
// https://eli.thegreenplace.net/2016/c11-threads-affinity-and-hyperthreading
// On windows: SetThreadIdealProcessor(t1.native_handle(), 3);

//$ TODO mikesart: Temporarily say we've got these functions...
#define HAVE_PTHREAD_H
#define HAVE_PTHREAD_SETNAME_NP

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

class ThreadPool
{
public:
    ThreadPool( unsigned int num_threads = get_num_supported_hw_threads() )
    {
        num_threads = std::max< unsigned int >( 1, num_threads );

        worker_threads.reserve( num_threads );

        for ( unsigned int threadid = 0; threadid < num_threads; ++threadid )
        {
            auto thread = std::thread( &ThreadPool::worker_thread_func, this, threadid );

#ifdef HAVE_PTHREAD_SETNAME_NP
            std::string thread_name = "thpool" + std::to_string( threadid );
            pthread_setname_np( thread.native_handle(), thread_name.c_str() );
#endif

            worker_threads.push_back( std::move( thread ) );
        }
    }

    ~ThreadPool()
    {
        shutdown = true;
        conditional_lock.notify_all();

        for ( std::thread &worker : worker_threads )
        {
            if ( worker.joinable() )
                worker.join();
        }
    }

    void worker_thread_func( unsigned int threadid )
    {
        for ( ;; )
        {
            Job job;

            {
                std::unique_lock< std::mutex > lock( job_queue_mutex );

                conditional_lock.wait( lock, [this] { return shutdown || !job_queue.empty(); } );
                if ( shutdown )
                    break;

                job = std::move( job_queue.front() );
                job_queue.pop();
            }

            //$ TODO mikesart: printf( "Threadid %u executing job '%s'...\n", threadid, job.name.c_str() );

            job.func();
        }
    }

    template < typename Func, typename... Args >
    auto submit_job( const std::string &job_name, Func &&func, Args &&... args ) ->
        std::future< decltype( func( args... ) ) >
    {
        using ret_type = decltype( func( args... ) );
        std::function< ret_type() > bound_func = std::bind(
                std::forward< Func >( func ), std::forward< Args >( args )... );
        auto task = std::make_shared< std::packaged_task< ret_type() > >( bound_func );
        std::future< ret_type > result = task->get_future();

        {
            Job job;
            std::unique_lock< std::mutex > lock( job_queue_mutex );

            job.name = job_name;
            job.func = [task]() { ( *task )(); };
            job_queue.push( std::move( job ) );
        }

        conditional_lock.notify_one();
        return result;
    }

    size_t get_num_threads()
    {
        return worker_threads.size();
    }

    static unsigned int get_num_supported_hw_threads()
    {
        // Returns the number of concurrent threads supported by the implementation.
        // If value is not well defined or not computable, returns 0.
        return std::thread::hardware_concurrency();
    }

private:
    ThreadPool( const ThreadPool & ) = delete;
    ThreadPool( ThreadPool && ) = delete;

    ThreadPool &operator=( const ThreadPool & ) = delete;
    ThreadPool &operator=( ThreadPool && ) = delete;

private:
    struct Job
    {
        std::string name;
        std::function< void() > func;
    };
    std::queue< Job > job_queue;
    std::mutex job_queue_mutex;

    std::condition_variable conditional_lock;

    std::vector< std::thread > worker_threads;

    // Threadpool shutting down?
    std::atomic_bool shutdown;
};

#endif /* THREADPOOL_H */
