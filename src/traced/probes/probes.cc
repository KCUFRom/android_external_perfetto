/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/traced/traced.h"

#include "src/ftrace_reader/ftrace_procfs.h"
#include "src/traced/probes/probes_producer.h"
#include "src/tracing/ipc/default_socket.h"

namespace perfetto {

int __attribute__((visibility("default"))) ProbesMain(int argc, char** argv) {
  static struct option long_options[] = {
      {"cleanup-after-crash", no_argument, nullptr, 'd'},
      {nullptr, 0, nullptr, 0}};
  int option_index;
  int c;
  while ((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
    switch (c) {
      case 'd':
        HardResetFtraceState();
        return 0;
      default:
        PERFETTO_ELOG("Usage: %s [--cleanup-after-crash]", argv[0]);
        return 1;
    }
  }

  // Set the watchdog to kill the process if we average more than 32MB of
  // memory or 75% CPU over a 30 second window.
  base::Watchdog* watchdog = base::Watchdog::GetInstance();
  watchdog->SetCpuLimit(75, 30 * 1000);
  watchdog->SetMemoryLimit(32 * 1024 * 1024, 30 * 1000);
  watchdog->Start();

  PERFETTO_LOG("Starting %s service", argv[0]);

  // This environment variable is set by Android's init to a fd to /dev/kmsg
  // opened for writing (see perfetto.rc). We cannot open the file directly
  // due to permissions.
  const char* env = getenv("ANDROID_FILE__dev_kmsg");
  if (env) {
    FtraceProcfs::g_kmesg_fd = atoi(env);
    // The file descriptor passed by init doesn't have the FD_CLOEXEC bit set.
    // Set it so we don't leak this fd while invoking atrace.
    int res = fcntl(FtraceProcfs::g_kmesg_fd, F_SETFD, FD_CLOEXEC);
    PERFETTO_DCHECK(res == 0);
  }

  base::UnixTaskRunner task_runner;
  ProbesProducer producer;
  producer.ConnectWithRetries(GetProducerSocket(), &task_runner);
  task_runner.Run();
  return 0;
}

}  // namespace perfetto
