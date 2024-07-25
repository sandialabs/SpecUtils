/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "SpecUtils_config.h"

// Block out some warnings occurring in xutility.
// warning C4996: function call with parameters that may be unsafe -
#pragma warning(disable:4996)

#include <string>
#include <thread>
#include <iostream>
#include <assert.h>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <Lmcons.h>
#elif __APPLE__
#include <sys/sysctl.h>
#else
#include <unistd.h>
#endif


#include "SpecUtils/SpecUtilsAsync.h"


using namespace std;


namespace SpecUtilsAsync
{
#if( defined(ThreadPool_USING_WT) || defined(ThreadPool_USING_GCD) )
  std::mutex ThreadPool::sm_npool_mutex;
  int ThreadPool::sm_npools = 0;
#endif

  
  int num_logical_cpu_cores()
  {
#if( SpecUtils_USING_NO_THREADING )
    return 1;
#else
    return max( 1, static_cast<int>( std::thread::hardware_concurrency() ) );
#endif
  }//int num_logical_cpu_cores()


  int num_physical_cpu_cores()
  {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (std::max)( DWORD(1), sysinfo.dwNumberOfProcessors );
#elif __APPLE__
    int physicalCores;
    size_t physicalCoresMemSize = sizeof(physicalCores);
    sysctlbyname("hw.physicalcpu", &physicalCores, &physicalCoresMemSize, NULL, 0);
    return max( 1, physicalCores );
#else
    return max( 1, static_cast<int>( sysconf(_SC_NPROCESSORS_ONLN) ) );
#endif
  }//int num_physical_cpu_cores()
  
#if( defined(ThreadPool_USING_GCD) )
#elif( defined(ThreadPool_USING_WT) )
#elif( defined(ThreadPool_USING_THREADS) )
#elif( defined(ThreadPool_USING_SERIAL ) )
#endif
  
  ThreadPool::ThreadPool()
  {
#if( defined(ThreadPool_USING_GCD) )
    {// Begin lock on sm_npool_mutex
      std::unique_lock<std::mutex> lock( sm_npool_mutex );
      sm_npools += 1;
      
      // Looks like GCD currently has a max number of 64 threads, so if we create more threads than
      //  this that depend upon each other (e.g., recursive use of ThreadPools), there could be a
      //  chance we deadlock.  To avoid this, and not be too unreasonable, we'll cap the number
      //  of ThreadPool objects at 48 (since this is already crazy territory).
      m_canSubmitToGCD = (sm_npools < 48);
    }// End lock on sm_npool_mutex
    
    if( m_canSubmitToGCD )
      m_queue = dispatch_queue_create("InterSpec.Sandia.ThreadPool", DISPATCH_QUEUE_CONCURRENT);
    else
      m_queue = nullptr; //dispatch_get_global_queue( QOS_CLASS_DEFAULT, 0 );
    
#elif( defined(ThreadPool_USING_WT) )
    m_nJobsLeft = 0;
    
    Wt::WServer *server = Wt::WServer::instance();
    
    std::unique_lock<std::mutex> lock( sm_npool_mutex );
    sm_npools += 1;
    
    //Will assume thread count of WIOService() never changes
    m_canSubmitToWtPool = (server && ((server->ioService().threadCount()-sm_npools)>2));
#elif( defined(ThreadPool_USING_THREADS) )
#elif( defined(ThreadPool_USING_SERIAL ) )
#endif
  }
  
  ThreadPool::~ThreadPool()
  {
    // Throwing an exception from a destructor is dangerous
    try
    {
      join();
    }catch( std::exception & )
    {
      m_exception = std::current_exception();
    }
    
    {// begin lock on m_exception_mutex
      std::lock_guard<std::mutex> lock( m_exception_mutex );
      if( m_exception )
      {
        try
        {
          std::rethrow_exception(m_exception);
        }catch( std::exception &e )
        {
          assert( 0 );
          const string err_msg = "ThreadPool destructor called with a pending exception: \""
                                  + std::string(e.what()) + "\"";
          cerr << err_msg << endl;
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
          log_developer_error( __func__, err_msg.c_str() );
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
        }catch( ... )
        {
          assert( 0 );
          const string err_msg = "ThreadPool destructor called with a pending non std exception.";
          cerr << err_msg << endl;
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
          log_developer_error( __func__, err_msg.c_str() );
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
        }//try / catch /catch
        
        m_exception = nullptr;
      }//if( m_exception )
    }// end lock on m_exception_mutex
    
#if( defined(ThreadPool_USING_WT) )
    std::unique_lock<std::mutex> lock( sm_npool_mutex );
    sm_npools -= 1;
#endif
    
#if( defined(ThreadPool_USING_GCD) )
    if( m_canSubmitToGCD )
      dispatch_release( m_queue );
    
    {// Begin lock on sm_npool_mutex
      std::unique_lock<std::mutex> lock( sm_npool_mutex );
      sm_npools -= 1;
    }// End lock on sm_npool_mutex
#endif
  }//~ThreadPool()
  
  
  
  void ThreadPool::join()
  {
#if( defined(ThreadPool_USING_GCD) )
    if( m_canSubmitToGCD )
    {
      assert( m_nonPostedWorkers.empty() );
      
      dispatch_barrier_sync( m_queue, ^{} );
    }else
    {
      if( m_nonPostedWorkers.size() )
      {
        do_asyncronous_work( m_nonPostedWorkers, false );
        m_nonPostedWorkers.clear();
      }
    }//if( m_canSubmitToGCD ) / else
    
    std::lock_guard<std::mutex> lock( m_exception_mutex );
    if( m_exception )
    {
      std::exception_ptr the_exception = m_exception;
      m_exception = nullptr;
      std::rethrow_exception( the_exception );
    }
#elif( defined(ThreadPool_USING_WT) )
    {
      std::unique_lock<std::mutex> lock( m_mutex );
      while( m_nJobsLeft )
      {
        m_condition.wait( lock );
      }
    }
    
    if( m_nonPostedWorkers.size() )
    {
      do_asyncronous_work( m_nonPostedWorkers, false );
      m_nonPostedWorkers.clear();
    }
    
    std::lock_guard<std::mutex> lock( m_exception_mutex );
    if( m_exception )
    {
      std::exception_ptr the_exception = m_exception;
      m_exception = nullptr;
      std::rethrow_exception( the_exception );
    }
#elif( defined(ThreadPool_USING_THREADS) )
    if( m_nonPostedWorkers.size() )
    {
      do_asyncronous_work( m_nonPostedWorkers, false );
      m_nonPostedWorkers.clear();
    }
    std::lock_guard<std::mutex> lock( m_exception_mutex );
    if( m_exception )
      std::rethrow_exception( m_exception );
#elif( defined(ThreadPool_USING_SERIAL ) )
#endif
  }//void join()
  
  
#if( defined(ThreadPool_USING_WT) || defined(ThreadPool_USING_THREADS) || defined(ThreadPool_USING_GCD) )
  
  void ThreadPool::doworkasync( const std::function< void(void) > &fcn )
  {
    try
    {
      fcn();
    }catch( std::exception &e )
    {
      std::lock_guard<std::mutex> lock( m_exception_mutex );
      std::cerr << "ThreadPool::dowork caught: " << e.what() << std::endl;
      m_exception = std::current_exception();
    }
  }//void ThreadPool::doworkasync( const std::function< void(void) > &fcn )
  
  
  void ThreadPool::dowork( const std::function< void(void) > &fcn )
  {
    try
    {
      fcn();
    }catch( std::exception &e )
    {
      std::lock_guard<std::mutex> lock( m_exception_mutex );
      std::cerr << "ThreadPool::dowork caught: " << e.what() << std::endl;
      m_exception = std::current_exception();
    }
    
#if( defined(ThreadPool_USING_WT) )
    bool notify = false;
    
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      --m_nJobsLeft;
      notify = (m_nJobsLeft==0);
    }
    
    if( notify )
      m_condition.notify_all();
#endif //#if( SpecUtils_USE_WT_THREADPOOL )
  }//void dowork(...)
#endif  //#if( defined(ThreadPool_USING_WT) || defined(ThreadPool_USING_THREADS) )
  
  
  void ThreadPool::post( std::function< void(void) > worker )
  {
#if( defined(ThreadPool_USING_GCD) )
    if( m_canSubmitToGCD )
    {
      dispatch_async( m_queue,
                     ^{
        try
        {
          worker();
        }catch( std::exception &e )
        {
          std::lock_guard<std::mutex> lock( m_exception_mutex );
          //if( m_exception ) std::cerr << "ThreadPool::dowork async has caught multiple exceptions\n";
          //std::cerr << "ThreadPool::dowork async caught: " << e.what() << std::endl;
          m_exception = std::current_exception();
        }
      } );
    }else
    {
      m_nonPostedWorkers.push_back( std::bind( &ThreadPool::doworkasync, this, worker ) );
    }//if( m_canSubmitToGCD ) / else
#elif( defined(ThreadPool_USING_WT) )
    Wt::WServer *server = Wt::WServer::instance();
    
    if( m_canSubmitToWtPool && server )
    {
      Wt::WIOService &io = server->ioService();
      
      {
        std::lock_guard<std::mutex> lock( m_mutex );
        ++m_nJobsLeft;
      }
      
      // Starting with Wt commit e3726036 (https://github.com/emweb/wt/commit/e372603613022094729f81374be98a4542446d87)
      //  (Mar 30, 2016) `Wt::WIOService::post(...)` started posting to a strand, so only a single
      //  posted function would execute at a time.  This means if we were to post to this strand
      //  and create a ThreadPool from within another ThreadPools workers, things will deadlock.
      //  So instead, because WIOService inherits from boost::asio::io_service we will use
      //  `boost::asio::io_service::post` to post the work
      io.boost::asio::io_service::post( [this,worker](){
        this->dowork(worker);
      } );
    }else
    {
      m_nonPostedWorkers.push_back( std::bind( &ThreadPool::doworkasync, this, worker ) );
    }
#elif( defined(ThreadPool_USING_THREADS) )
    m_nonPostedWorkers.push_back( std::bind(&ThreadPool::doworkasync, this, worker) );
#elif( defined(ThreadPool_USING_SERIAL ) )
    try
    {
      worker();
    }catch( std::exception &e )
    {
      std::lock_guard<std::mutex> lock( m_exception_mutex );
      //if( m_exception ) std::cerr << "ThreadPool::dowork async has caught multiple exceptions\n";
      //std::cerr << "ThreadPool::dowork async caught: " << e.what() << std::endl;
      m_exception = std::current_exception();
    }
#endif
  }//void ThreadPool::post( std::function<void(void)> fcn )

} //namespace SpecUtilsAsync
  
  
