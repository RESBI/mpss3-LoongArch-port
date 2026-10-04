/*
 * Copyright 2010-2017 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, version 2.1.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Disclaimer: The codes contained in these modules may be specific
 * to the Intel Software Development Platform codenamed Knights Ferry,
 * and the Intel product codenamed Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel offers no warranty of any kind regarding the code. This code is
 * licensed on an "AS IS" basis and Intel is not obligated to provide
 * any support, assistance, installation, training, or other services
 * of any kind. Intel is also not obligated to provide any updates,
 * enhancements or extensions. Intel specifically disclaims any warranty
 * of merchantability, non-infringement, fitness for any particular
 * purpose, and any other warranty.
 *
 * Further, Intel disclaims all liability of any kind, including but
 * not limited to liability for infringement of any proprietary rights,
 * relating to the use of the code, even if Intel is notified of the
 * possibility of such liability. Except as expressly stated in an Intel
 * license agreement provided with this code and agreed upon with Intel,
 * no license, express or implied, by estoppel or otherwise, to any
 * intellectual property rights is granted herein.
 */

#include <internal/_COICommFactory.h>
#include <internal/_COIComm.h>
#include <internal/_Process.h>
#ifdef TRANSPORT_OFI
    #include <internal/_COISecurity.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <linux/magic.h>
#include <sys/vfs.h>

#include <string.h>
#include <sstream>

#include <dirent.h>
#include <unistd.h>
#include <stdint.h>

#include <fcntl.h>
#include <signal.h>
#include <exception>
#include <pwd.h>
#include <errno.h>

#include "daemon.h"
#include "util.h"


#define COI_USER_TMP_PATH_MAX 256
COI_COMM_TYPE node_type = COI_COMM_INVALID;

#define xstr(s) str(s)
#define str(s) #s

#define MAX_SCIF_PORT   65536

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

// loadcalc.cpp
extern void loadcalc_shutdown();

static void signal_handler(int sig, siginfo_t *si, void *)
{
    if (sig == SIGQUIT)
    {
        FATAL("caught %s(%s)\n", strsigsym(sig),
              strsiginfocode(si->si_signo, si->si_code));
    }
    else
    {
        FATAL("caught %s(%s) on address %p\n", strsigsym(sig),
              strsiginfocode(si->si_signo, si->si_code), si->si_addr);
    }
}

static void print_usage(void)
{
#define FIRST_INDENT " "
#define NORMAL_INDENT "                           "
    fprintf(stderr, "usage: coi_daemon [OPTIONS]\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "  %-24s"
            FIRST_INDENT
            "This option enables output from the daemon and\n"
            NORMAL_INDENT
            "child (sink) processes without a proxy to be sent\n"
            NORMAL_INDENT
            " to <stream>. Without this option output gets sent\n"
            NORMAL_INDENT " to /dev/null\n"
            NORMAL_INDENT
            "--enable-output is a long alias for --\n"
            , "--[=<stream>]");

    fprintf(stderr, "\n");
    fprintf(stderr, "  %-24s"
            FIRST_INDENT
            "The maximum number of connections we allow from host\n"
            NORMAL_INDENT
            "processes. If this is exceeded, new connections\n"
            NORMAL_INDENT
            "are temporarily blocked. Defaults to %d.\n"
            , "--max-connections=<int>", DEFAULT_MAX_CONNECTIONS);

    fprintf(stderr, "\n");
    fprintf(stderr, "  %-24s"
            FIRST_INDENT
            "A valid system username. If this is used the coi_daemon\n"
            NORMAL_INDENT
            "will set priviledges to this user after startup.\n\n"
            NORMAL_INDENT
            "Specifying '%s' will enable authentication.\n"
            NORMAL_INDENT
            "The coi_daemon will attempt to validate users who use the coi_daemon.\n"
            NORMAL_INDENT
            "Unauthorized users will be unable to use the coi_daemon,\n"
            NORMAL_INDENT
            "and processes created by the coi_daemon will have the permissions\n"
            NORMAL_INDENT
            "of the user that requested its creation.\n"
            NORMAL_INDENT
            "For MS Windows host, usernames are modified as follows:\n"
            NORMAL_INDENT
            "  * Spaces converted to underscores, i.e.'_'\n"
            NORMAL_INDENT
            "  * All character converted to lowercase\n\n"
            NORMAL_INDENT
            "Defaults to '%s'.\n"
            , "--coiuser=<username>", "_Authorized", DEFAULT_USER);

    fprintf(stderr, "\n");
    fprintf(stderr, "  %-24s"
            FIRST_INDENT
            "A valid port number. If this is used the coi_daemon will attempt\n"
            NORMAL_INDENT
            "to bind to this port. Additionally COI_DAEMON_PORT needs to be\n"
            NORMAL_INDENT
            "exported on the host with this same value so the host application\n"
            NORMAL_INDENT
            "will be able connect with the coi_daemon. \n"
            , "--coiport=<port>");

    fprintf(stderr, "\n");
    fprintf(stderr, "  %-24s"
            FIRST_INDENT
#ifdef ENABLE_LOGGING
            "Where to write the log file to. See the <stream>\n"
            NORMAL_INDENT
            "definition below.\n"
            NORMAL_INDENT
            "indicates stdout, - means stderr. By default no\n"
            NORMAL_INDENT
            "log is generated.\n"
#else
            "Logging disabled, this argument will be ignored.\n"
#endif
            , "--log[=<stream>]");
    fprintf(stderr, "\n  %-24s"
            FIRST_INDENT "Can be an existing stream, a file or socket\n"
            NORMAL_INDENT "No argument means to use stdout. An argument of -\n"
            NORMAL_INDENT "means stderr. A path indicates a file and a\n"
            NORMAL_INDENT "host:port pair opens a socket.\n"
            NORMAL_INDENT "EXAMPLES:\n"
            NORMAL_INDENT " --               (stdout)\n"
            NORMAL_INDENT " --=-             (stderr)\n"
            NORMAL_INDENT " --=/tmp/bar.log  (a file)\n"
            NORMAL_INDENT " --=host:27001    (a socket).\n"
            NORMAL_INDENT "        E.g: first run (nc -l 27001) on host\n"
            , "<stream>"
           );
    fprintf(stderr, "\n");
    fprintf(stderr, "  %-24s"
            NORMAL_INDENT
            "A valid Temp Directory to be used as the base directory for all\n"
            NORMAL_INDENT
            "temp files created by this Daemon. This option overrides the setting\n"
            NORMAL_INDENT
            "for the temp directory to be /tmp/ or /home/<user>/ for the _Authorized.\n"
            NORMAL_INDENT
            "Example: --tempDIR=/mytmp\n", "--tempDIR=<path_to_dir>\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  %-24s"
            NORMAL_INDENT
            "COI authentication mode one of noauth or munge\n"
            NORMAL_INDENT
            "noauth - no authentication, not secure connection\n"
            NORMAL_INDENT
            "munge  - MUNGE (MUNGE Uid 'N' Gid Emporium) authentication, requires MUNGE environment\n"
            //NORMAL_INDENT
            //"ssh mode should never be used direct by user\n"
            NORMAL_INDENT "EXAMPLES:\n"
            NORMAL_INDENT " --auth-mode=noauth\n", "--auth-mode=[noauth,munge]\n");
    fprintf(stderr, "\n");
}

// atoi and strtol admit some invalid input e.g. atoi("123asdkfj")
// Neither reports errors, they simply return 0.
static bool parse_int(const char *arg, int *i)
{
    int sign = 1;
    int x = 0;
    int n = 0;

    if (!arg)
    {
        WARN("%s: arg was NULL\n", __FUNCTION__);
        return false;
    }

    if (!i)
    {
        WARN("%s: i was NULL\n", __FUNCTION__);
        return false;
    }

    if (*arg == '-')
    {
        arg++;
        sign = -1;
    }

    if (sscanf(arg, "%d%n", &x, &n) == 0 ||
            ((unsigned int)n < strlen(arg) &&
             arg[n] != 0))
    {
        return false;
    }

    *i = sign * x;

    return true;
}

static bool parse_sock_stream(const char *str, struct sockaddr_in *a)
{
    int port = -1;
    const char *portstr = NULL;
    const char *host = NULL;
    char hostPortStr[256] = {0};
    if (!str)
    {
        WARN("%s: str was NULL\n", __FUNCTION__);
        return false;
    }

    if (!a)
    {
        WARN("%s: a was NULL\n", __FUNCTION__);
        return false;
    }

    strncpy(hostPortStr, str, sizeof(hostPortStr));
    hostPortStr[sizeof(hostPortStr) - 1] = '\0';
    host = strtok(hostPortStr, ":");
    portstr = strtok(NULL, ":");

    if (portstr == NULL || host == NULL || !parse_int(portstr + 1, &port) ||
            port < 0 || port > MAX_SCIF_PORT)
    {
        return false;
    }

    return (0 == resolve_addr(host, (unsigned short)port, a));
}


// Parses a stream argument and opens or redirects a file descriptor for
// that stream. Examples:
// --foo                  (no arg) means use STDOUT_FILENO
// --foo=-                - mean suse STDERR_FILENO
// --foo=file             file means use file
// --foo=somehost:27001   open a socket to somehost on port 27001
//
// val points to whatever follows the = (or NULL or the empty string
// "--foo=file"                          for no argument)
//        ^  val
//
// The dup_stdio flag says that if the stream used is a stdio stream, dup it.
int openstream(const char *val, bool dup_stdio)
{
    if (val == NULL || *val == 0)
    {
        if (dup_stdio)
        {
            return dup(STDOUT_FILENO);
        }
        else
        {
            return STDOUT_FILENO;
        }
    }
    else if (strcmp(val, "-") == 0)
    {
        if (dup_stdio)
        {
            return dup(STDERR_FILENO);
        }
        else
        {
            return STDERR_FILENO;
        }
    }
    else
    {
        struct sockaddr_in raddr;
        if (parse_sock_stream(val, &raddr))
        {
            return connect_socket(&raddr, 2000);
        }
        else
        {
            return open(val, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }
    }
}


int main(int argc, char **argv)
{
    char coi_user[MAX_USERNAME_LENGTH];
    bool coi_user_set = false;
    int bCustomUser = 0;
    int max_connections = DEFAULT_MAX_CONNECTIONS;
    char coi_port[COI_MAX_PORT] = {0};
    char coi_address[COI_MAX_ADDRESS] = {0};
    int output_fd = -1;
    bool authorized_mode = false;
    char user_defined_temp_dir[COI_USER_TMP_PATH_MAX] = {0};
#ifndef TRANSPORT_OFI
    node_type = COI_SCIF_NODE;
#else
    node_type = COI_OFI_NODE;
    char nonce[COI_AUTH_DATA_MAX_LEN] = {0};
    bool is_coiport_set   = false;
    bool is_auth_mode_set = false;

    // n case of empty commandline print usage and exit
    if (argc == 1)
    {
        print_usage();
        return -1;
    }
#endif
    for (int ai = 1; ai < argc; ai++)
    {
        const char *arg = "";
        if (strprefix(argv[ai], "--help", NULL))
        {
            print_usage();
            return -1;
        }
        else if (strprefix(argv[ai], "--max-connections=", &arg))
        {
            if (!parse_int(arg, &max_connections))
            {
                fprintf(stderr, "malformed connection argument: \'%s\'\n", arg);
                exit(-1);
            }
        }
#ifdef TRANSPORT_OFI
        else if (strprefix(argv[ai], "--auth-mode=", &arg))
        {
            COIRESULT result = COISecurity::Initialize(arg);
            if (result == COI_NOT_SUPPORTED)
            {
                fprintf(stderr, "'%s' is an invalid auth mode, terminating.\n", arg);
                exit(-1);
            }
            else if (result != COI_SUCCESS)
            {
                fprintf(stderr, "error: Failed to initialize coi authentication\n");
                exit(-1);
            }
            is_auth_mode_set = true;
        }
#endif
        else if (strprefix(argv[ai], "--coiuser=", &arg))
        {
            if (coi_user_set)
            {
                fprintf(stderr, "Cannot set permissions of a process to multiple users!\n");
                return -1;
            }
            coi_user_set = true;

            size_t usersize = strlen(arg);
            if ((usersize > 0) && (usersize < MAX_USERNAME_LENGTH))
            {
                if (strncmp(arg, "_Authorized", sizeof(coi_user) - 1) == 0)
                {
                    authorized_mode = true;
                }
                else if (strncpy(coi_user, arg, sizeof(coi_user) - 1))
                {
                    bCustomUser = 1;
                    coi_user[sizeof(coi_user) - 1] = 0;
                }
                else
                {
                    fprintf(stderr, "strncpy failed to get custom user\n");
                }
            }
            else
            {
                fprintf(stderr, "'%s': is an invalid username, terminating\n",
                        arg);
                print_usage();
                return -1;
            }
        }
        else if (strprefix(argv[ai], "--coiport=", &arg))
        {
            uint16_t coi_port_tmp;
            stringstream ss(arg);
            ss >> coi_port_tmp;
            //ss returns null if the first term of coi_port_tmp isn't a number.
            //ss ignores everything that isn't a number past the first term
            //ss returns null if the number is > 65535
            //ss reroutes negatives to be added to 65535
            //don't allow the use of port 0.
            if (!ss || coi_port_tmp < 1)
            {
                fprintf(stderr,
                        "'%s': is an invalid port number, terminating\n",
                        arg);
                return -1;
            }
            strncpy(coi_port, arg, COI_MAX_PORT);
#ifdef TRANSPORT_OFI
            is_coiport_set = true;
#endif
        }
        else if (strcmp(argv[ai], "--") == 0 ||
                 strcmp(argv[ai], "--enable-output") == 0 ||
                 strprefix(argv[ai], "--=", &arg) ||
                 strprefix(argv[ai], "--enable-output=", &arg))
        {
            if (output_fd != -1)
            {
                fprintf(stderr, "-- option specified multiple times\n");
                return -1;
            }
            else if ((output_fd = openstream(arg, false)) == -1)
            {
                fprintf(stderr, "%s: failed to open stream: %s\n",
                        argv[ai], strerror(errno));
                return -1;
            }
        }
        else if (strprefix(argv[ai], "--coiaddress=", &arg))
        {
#ifndef TRANSPORT_OFI
            strncpy(coi_address, arg, COI_MAX_ADDRESS);
            coi_address[COI_MAX_ADDRESS - 1] = '\0';
#else
            size_t coiaddress_len = strlen(arg);
            if (coiaddress_len > 0)
            {
                strncpy(coi_address, arg, COI_MAX_ADDRESS);
                coi_address[COI_MAX_ADDRESS - 1] = '\0';
            }
            else
            {
                fprintf(stderr,
                        "'%s': is an invalid address number, terminating\n",
                        arg);
                return -1;
            }
#endif
        }
        else if (strcmp(argv[ai], "--log") == 0 ||
                 strprefix(argv[ai], "--log=", &arg))
        {
            int fd = -1;
#ifdef ENABLE_LOGGING
            if (g_log_file != NULL)
            {
                fprintf(stderr, "--log option specified multiple times\n");
                return -1;
            }
            else if ((fd = openstream(arg, true)) == -1 ||
                     (g_log_file = fdopen(fd, "w")) == NULL)
            {
                if (arg != 0)
                {
                    fprintf(stderr, "--log failed to open %s: %s\n", arg,
                            strerror(errno));
                }
                return -1;
            }
            setCLOEXEC(fd);
#else
            const char *errmsg = "ignoring --log argument. Compile"
                                 " with ENABLE_LOGGING to enable output\n";
            fprintf(stderr, errmsg);

            if (!g_log_file)
            {
                if ((fd = openstream(arg, false)) == -1 ||
                        (g_log_file = fdopen(fd, "w")) == NULL)
                {
                    if (arg != 0)
                    {
                        fprintf(stderr, "--log failed to open %s: %s\n", arg,
                                strerror(errno));
                    }
                    if ((fd > -1) && close(fd)) //potentially this was opened
                    {
                        fprintf(stderr, "Failed to close log file\n");
                        return -1;
                    }
                }
                else
                {
                    // in case they go looking for their log file
                    fprintf(g_log_file, errmsg);
                    fflush(g_log_file);
                    fclose(g_log_file);
                    g_log_file = NULL;
                    if (close(fd))
                    {
                        fprintf(stderr, "Failed to close log file\n");
                        return -1;
                    }
                }
            }
            else
            {
                fprintf(stderr, "--log option specified multiple times\n");
                return -1;
            }
#endif
        }
        else if (strprefix(argv[ai], "--tempDIR=", &arg))
        {
            if (strlen(arg) >= COI_USER_TMP_PATH_MAX)
            {
                fprintf(stderr, "Temp directory name is too long!\n");
                return -1;
            }

            strncpy(user_defined_temp_dir, arg, sizeof(user_defined_temp_dir) - 1);
            DIR *dir = opendir(user_defined_temp_dir);
            if (dir)
            {
                closedir(dir);
            }
            else
            {
                if (ENOENT == errno)
                {
                    fprintf(stderr, "Temp directory does not exist!\n");
                }
                else
                {
                    fprintf(stderr, "Temp directory openning problem!\n");
                }
                user_defined_temp_dir[0] = '\0';
                return -1;
            }
        }
        else
        {
            fprintf(stderr, "unrecognized argument \'%s\'\n", argv[ai]);
            print_usage();
            return -1;
        }
    }

    int retCode = 0;
    struct statfs statfsbuf;
#ifdef TRANSPORT_OFI

    if (!is_auth_mode_set)
    {
        fprintf(stderr, "Missing --auth-mode parameter, terminating.\n");
        return -1;
    }

    if (!is_coiport_set)
    {
        fprintf(stderr, "Missing --coiport parameter, terminating.\n");
        return -1;
    }

    if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH)
    {
        max_connections = 1;
    }

    //check if TMP directory already exists, if not mkdir
    retCode = access(_COISinkProcessCommon::GetTmpPath(), F_OK);
    if (retCode)
    {
        retCode = mkdir(_COISinkProcessCommon::GetTmpPath(), S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO);
        if (retCode)
        {
            FATAL("mkdir %s failed with msg: %s", _COISinkProcessCommon::GetTmpPath(), strerror(errno));
        }
        INFO("mkdir %s success\n",  _COISinkProcessCommon::GetTmpPath());
    }

    //getTmpPath status to check if destination is already the tmpfs
    retCode = statfs(_COISinkProcessCommon::GetTmpPath(), &statfsbuf);

    if (retCode)
    {
        WARN("statvfs on %s failed with: %s, I ignore it and proceed\n", _COISinkProcessCommon::GetTmpPath(), strerror(errno));
    }

    //check if getTmpPath destination is tmpfs. If not, try to mount it.
    if (statfsbuf.f_type != TMPFS_MAGIC)
    {
        INFO("%s is not tmpfs, trying to mount...", _COISinkProcessCommon::GetTmpPath());
        //we assume that below line exists in /etc/fstab
        //tmpfs /tmp/intel-coi tmpfs  defaults,user,exec,size=32g,mode=1777 0 0
        retCode = system((std::string("mount ") + std::string(_COISinkProcessCommon::GetTmpPath())).c_str());
        if (retCode == -1)
        {
            FATAL("command system(mount) failed\n");
            return -1;
        }
        else if (retCode != 0)
        {
            FATAL("mount failed with retCode:%d\n", retCode);
            return -1;
        }

        INFO("OK\n");
    }

    //check if ProcessPath exists, if not create it.
    retCode = access(_COISinkProcessCommon::GetProcsPath(), F_OK);
    if (retCode)
    {
        INFO("%s dir does not exist, creating...\t", _COISinkProcessCommon::GetProcsPath());
        retCode = mkdir(_COISinkProcessCommon::GetProcsPath(), S_IRWXU | S_IRWXG | S_IRWXO);
        if (retCode)
        {
            FATAL("mkdir fail: %s\n", strerror(errno));
            return -1;
        }
        INFO("OK\n");
    }
#endif

    //check if hugeTLBPath exists, if not create it.
    retCode = access(_COISinkProcessCommon::GetHugeTLBfsPath(), F_OK);
    if (retCode)
    {
        INFO("%s dir does not exist, creating...\t", _COISinkProcessCommon::GetHugeTLBfsPath());
        retCode = mkdir(_COISinkProcessCommon::GetHugeTLBfsPath(), S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO);
        if (retCode)
        {
            FATAL("mkdir fail: %s\n", strerror(errno));
            return -1;
        }
        INFO("OK\n");
    }

    //getHugeTLBfsPath status to check if destination is already the HugeTLBfs
    retCode = statfs(_COISinkProcessCommon::GetHugeTLBfsPath(), &statfsbuf);

    if (retCode)
    {
        WARN("statvfs on %s failed with: %s, I ignore it and proceed\n", _COISinkProcessCommon::GetHugeTLBfsPath(), strerror(errno));
    }

    if (statfsbuf.f_type != HUGETLBFS_MAGIC)
    {
        INFO("%s is not hugetlbfs, trying to mount...", _COISinkProcessCommon::GetHugeTLBfsPath());

        //we assume that below line exists in /etc/fstab
        //hugetlbfs /tmp/intel-coi/COI2MB hugetlbfs defaults,user,mode=1777 0 0
#ifdef TRANSPORT_OFI
        //as in TRANSPORT_OFI we are not root user, posix::mount will not work
        //switching to system()
        retCode = system((std::string("mount ") + std::string(_COISinkProcessCommon::GetHugeTLBfsPath())).c_str());
        if (retCode == -1)
        {
            FATAL("%s command system(mount) failed\n", _COISinkProcessCommon::GetHugeTLBfsPath());
        }
#else
        retCode = mount("none",
                        _COISinkProcessCommon::GetHugeTLBfsPath(),
                        "hugetlbfs",
                        0, NULL);
        if (retCode != 0)
        {
            FATAL("mount %s failed with retCode:%d\n", _COISinkProcessCommon::GetHugeTLBfsPath(), retCode);
        }
#endif

        INFO("OK\n");
        // Due due to legacy support and due to possible umask set, force new permission flags.
        retCode = chmod(_COISinkProcessCommon::GetHugeTLBfsPath(), S_IRWXU | S_IRWXG | S_IRWXO);
        if (retCode != 0)
        {
            INFO("Cannot set permission to buffer directory");
        }
    }
    // Drop permissions here to specified user, we don't want to open
    // any file descriptors while retaining super user priviledges
#ifdef TRANSPORT_OFI
    if (bCustomUser == 0)
    {
        getlogin_r(coi_user, MAX_USERNAME_LENGTH);
        bCustomUser = 1;
    }
    if (bCustomUser)
    {
        if (setcoiuser(coi_user))
        {
            fprintf(stderr, "Can't drop priviledges to requested user '%s'\n",
                    coi_user);
            return -1;
        }
    }
    else
    {
        if (setcoiuser(DEFAULT_USER))
        {
            fprintf(stderr, "Can't set user to default user: %s\n", DEFAULT_USER);
            return -1;
        }
    }

    if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH)
    {
        int retcode = isatty(STDIN_FILENO);
        if (retcode)
        {
            fprintf(stderr, "Daemon is started automatically in SSH mode, terminating.\n");
            return -1;
        }

        size_t expected_auth_data_len = COISecurity::GetInstance().GetAuthDataLength();
        if (read(STDIN_FILENO, nonce, expected_auth_data_len) != expected_auth_data_len)
        {
            fprintf(stderr, "Cannot read nonce from stdin!\n");
            exit(-1);
        }
    }

    pid_t child_pid = fork();
    if (child_pid < 0)
    {
        fprintf(stderr, "fork() return error!\n");
        exit(-1);
    }

    // We don't need parent anymore.
    if (child_pid > 0)
    {
        return 0;
    }

    // After fork we need to restart session.
    if (setsid() == -1)
    {
        fprintf(stderr, "setsid() return error!\n");
        exit(-1);
    }
#endif

    // Open the listener first. This gives the user a much better error message
    // for a common problem: the daemon is already running.
    // Also this must occur BEFORE the setcoiuser call, in case we are binding
    // to a priviledged port, which is always the case at the moment.
    _COIComm *listener = NULL;
    char *listener_port;

    try
    {
        char tmp_port[COI_MAX_PORT] = {0};
        uint32_t daemon_port;

        if (_COICommFactory::CreateCOIComm(node_type, &listener) != COI_SUCCESS)
        {
            throw COI_ERROR;
        }

        if (strcmp(coi_port, "") != 0)
        {
            listener_port = coi_port;
        }
        else
        {
            if (COI_SUCCESS != listener->GetDaemonDefaultPort(&daemon_port))
            {
                throw COI_ERROR;
            }
            snprintf(tmp_port, COI_MAX_PORT, "%d", daemon_port);
            listener_port = tmp_port;
        }

#ifndef TRANSPORT_OFI
        if (listener->BindAndListen(listener_port, max_connections) != COI_SUCCESS)
        {
            throw COI_ERROR;
        }
#else
        if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_NOAUTH ||  COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_MUNGE)
        {
            if (listener->BindAndListen(listener_port, max_connections) != COI_SUCCESS)
            {
                throw COI_ERROR;
            }
        }
#endif
    }
    catch (COIRESULT r)
    {
        // We don't need to dump diagnostic information for this error.
        fprintf(stderr, "Exception opening listener:\n  %s (%s): Is the daemon already"
                " running?\n", COIRESULTStr(r), strerror(errno));
        exit(1);
    }
#ifndef TRANSPORT_OFI
    // Drop permissions here to specified user, we don't want to open
    // any file descriptors while retaining super user priviledges
    if (bCustomUser)
    {
        if (setcoiuser(coi_user))
        {
            fprintf(stderr, "Can't drop priviledges to requested user '%s'\n",
                    coi_user);
            return -1;
        }
    }
    else
    {
        if (setcoiuser(DEFAULT_USER))
        {
            fprintf(stderr, "Can't set user to default user: %s\n", DEFAULT_USER);
            return -1;
        }
    }
#endif

    // If the proxy is not enabled, children inherit our stdout and stderr.
    // Since the daemon runs without a tty, when the children sinks try and
    // read or write, they will die from a broken pipe. Hence, we set our
    // stdout and stderr to /dev/null for them to inherit unless the user
    // explicitly demands output via the --enable-output option.
    // For stdin we always redirect it to /dev/null.
    int dev_null = -1;
    if ((dev_null = open("/dev/null", O_RDWR)) == -1)
    {
        FATAL("failed to open /dev/null: %s\n", strerror(errno));
    }

    if (output_fd == -1)
    {
        output_fd = dev_null;
        g_headless = true;
    }

    if (dup2(dev_null, STDIN_FILENO) == -1)
    {
        FATAL("dup2(.., STDIN_FILENO): %s", strerror(errno));
    }
    if (dup2(output_fd, STDOUT_FILENO) == -1)
    {
        FATAL("dup2(.., STDOUT_FILENO): %s", strerror(errno));
    }
    if (dup2(output_fd, STDERR_FILENO) == -1)
    {
        FATAL("dup2(.., STDERR_FILENO): %s", strerror(errno));
    }
    if (dev_null != -1)
    {
        if (close(dev_null))
        {
            FATAL("close(dev_null): %s", strerror(errno));
        }
    }

    std::string coi_lib_path;
    if (getenv("COI_LIBRARY_PATH"))
    {
        coi_lib_path = getenv("COI_LIBRARY_PATH");
    }
    else
    {
        std::stringstream strStream;
        strStream << "libcoi_device.so" << "." << COI_LIB_VERSION;
        strStream >> coi_lib_path;
    }
    INFO("--- Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI) DAEMON STARTED ---\n");
    INFO("  pid: %d\n", getpid());
    INFO("  built: %s %s\n", __DATE__, __TIME__);
    INFO("  lib-version: %d\n", COI_LIB_VERSION);
    INFO("  Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI) API VERSION: %s\n",
         COI_CONNECTION_API_VERSION_STR);

#ifndef TRANSPORT_OFI
    //  Pin the daemon to the BSP core.
    cpu_set_t *cpumask = get_daemon_cpu_set();
    if (sched_setaffinity(0, sizeof(*cpumask), cpumask) != 0)
    {
        WARN("sched_setaffinity: %s\n", strerror(errno));
    }
#endif

    // Install a fatal handler for SIGSEGV and SIGBUS'es
    struct sigaction sa;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sa.sa_flags = SA_SIGINFO;

    if (sigaction(SIGSEGV, &sa, NULL) ||
            sigaction(SIGBUS,  &sa, NULL) ||
            sigaction(SIGQUIT, &sa, NULL))
    {
        FATAL("Failed to install signal handler %s\n", strerror(errno));
    }
    g_coidaemon = new COIDaemon(max_connections, coi_lib_path.c_str(),
                                xstr(COI_DAEMON_ARCH), user_defined_temp_dir,
                                authorized_mode);
    // Execute the main loop to listen for, and process, connections.
    try
    {
        _COICommInfo connection_info;
#ifdef TRANSPORT_OFI
        if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH)
        {
            connection_info.SetParams(coi_address, coi_port, nonce);
        }
        else
        {
            connection_info.SetParams(coi_address, coi_port);
        }
#else
        connection_info.SetParams(coi_address, coi_port, getenv("COI_CONNECTION_NONCE"));
#endif

        g_coidaemon->MainLoop(listener, &connection_info);
    }
    catch (std::exception &e)
    {
        FATAL("Exception in MainLoop: (%s):\n", e.what());
    }
    catch (const char *err)
    {
        FATAL("Exception in MainLoop: (%s):\n", err);
    }
    catch (COIRESULT &r)
    {
        FATAL("Exception in MainLoop: %s\n", COIRESULTStr(r));
    }
    catch (...)
    {
        FATAL("Unknown exception in MainLoop\n");
    }

    loadcalc_shutdown();

    delete listener;

    if (g_log_file != NULL)
    {
        fflush(g_log_file);
    }

    delete g_coidaemon;
    g_coidaemon = NULL;

    return 0;
}
