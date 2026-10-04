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

#include <internal/_COISecurity.h>

int COISecurity::m_auth_mode = COISecurity::AUTH_UNINITIALIZED;

typedef munge_ctx_t (*munge_ctx_create_t)(void);
typedef void (*munge_ctx_destroy_t)(munge_ctx_t);
typedef munge_err_t (*munge_ctx_set_t)(munge_ctx_t ctx, munge_opt_t opt, ...);
typedef munge_err_t (*munge_encode_t)(char **cred, munge_ctx_t ctx, const void *buf, int len);
typedef munge_err_t (*munge_decode_t)(const char *cred, munge_ctx_t ctx, void **buf, int *len, uid_t *uid, gid_t *gid);
typedef const char *(*munge_strerror_t)(munge_err_t e);

// lengths of authentication data strings with null char
// at the end
enum
{
    NOAUTH_AUTH_DATA_LENGTH = 0,
    SSH_AUTH_DATA_LENGTH    = 32,
    MUNGE_AUTH_DATA_LENGTH  = 128
};

class MungeAuth: public COISecurity
{
public:
    MungeAuth(): COISecurity(MUNGE_AUTH_DATA_LENGTH)
    {
        munge_err_t  err;
        COIRESULT result;

        InitializeMunge();

        ctx = munge_ctx_create();

        m_uid = getuid();
        m_gid = getgid();

        err = munge_ctx_set(ctx, MUNGE_OPT_UID_RESTRICTION, m_uid);

        if (err != EMUNGE_SUCCESS)
        {
            throw std::runtime_error(munge_strerror(err));
        }

        std::string tmp_string;
        result = GetAuthData(tmp_string);
        if (result != COI_SUCCESS)
        {
            throw std::runtime_error("Failed to initialize munge\n");
        }

    }

    // dynamic loading of libmunge to avoid library dependency for users
    // who are not interested in such functionality
    void InitializeMunge()
    {
        void *handle;
        char *error;
        handle = dlopen("libmunge.so.2", RTLD_LAZY);
        if (!handle)
        {
            throw std::runtime_error(dlerror());
        }

        munge_ctx_create = (munge_ctx_create_t) dlsym(handle, "munge_ctx_create");
        if ((error = dlerror()) != NULL)
        {
            throw std::runtime_error(error);
        }

        munge_ctx_destroy = (munge_ctx_destroy_t) dlsym(handle, "munge_ctx_destroy");
        if ((error = dlerror()) != NULL)
        {
            throw std::runtime_error(error);
        }

        munge_ctx_set = (munge_ctx_set_t) dlsym(handle, "munge_ctx_set");
        if ((error = dlerror()) != NULL)
        {
            throw std::runtime_error(error);
        }

        munge_encode = (munge_encode_t) dlsym(handle, "munge_encode");
        if ((error = dlerror()) != NULL)
        {
            throw std::runtime_error(error);
        }

        munge_decode = (munge_decode_t) dlsym(handle, "munge_decode");
        if ((error = dlerror()) != NULL)
        {
            throw std::runtime_error(error);
        }

        munge_strerror = (const char *(*)(munge_err_t e)) dlsym(handle, "munge_strerror");
        if ((error = dlerror()) != NULL)
        {
            throw std::runtime_error(error);
        }

    }

    COIRESULT GetAuthData(std::string &cred)
    {
        munge_err_t  err;
        char        *cred_cstr;

        err = munge_encode(&cred_cstr, NULL, NULL, 0);

        if (err != EMUNGE_SUCCESS)
        {
            std::cerr << "munge credential generation failed:\tis Munge Daemon running?\n";
            return COI_ERROR;
        }

        cred = cred_cstr;
        free(cred_cstr);
        return COI_SUCCESS;
    }

    COIRESULT ValidateAuthData(const std::string &cred1, const char *cred2 = NULL)
    {
        munge_err_t  err;
        uid_t        received_uid;
        gid_t        received_gid;
        COIRESULT    result;

        err = munge_decode(cred1.c_str(), NULL, NULL, NULL, &received_uid, &received_gid);

        if (err != EMUNGE_SUCCESS)
        {
            std::cerr << munge_strerror(err) << std::endl;
            return COI_AUTHENTICATION_FAILURE;
        }

        if (m_uid == received_uid && m_gid == received_gid)
        {
            result = COI_SUCCESS;
        }
        else
        {
            result = COI_AUTHENTICATION_FAILURE;
        }
        return result;

    }

    ~MungeAuth()
    {
        munge_ctx_destroy(ctx);
    }

private:
    uid_t m_uid;
    gid_t m_gid;

    munge_ctx_create_t munge_ctx_create;
    munge_ctx_destroy_t munge_ctx_destroy;
    munge_ctx_set_t munge_ctx_set;
    munge_encode_t munge_encode;
    munge_decode_t munge_decode;
    munge_strerror_t munge_strerror;

    munge_ctx_t ctx;

};

class NoAuth: public COISecurity
{
public:
    NoAuth(): COISecurity(NOAUTH_AUTH_DATA_LENGTH) {}

    COIRESULT ValidateAuthData(const std::string &cred1, const char *cred2 = NULL)
    {
        return COI_SUCCESS;
    }
};

class SSHAuth: public COISecurity
{
public:
    SSHAuth(): COISecurity(SSH_AUTH_DATA_LENGTH)
    {
        m_binary_random = new char[m_auth_data_length];
    }

    COIRESULT GetAuthData(std::string &nonce)
    {
        nonce.resize(m_auth_data_length);
        FILE *urandom_handler = fopen("/dev/urandom", "r");
        if (!urandom_handler)
        {
            return COI_ERROR;
        }
        fread(m_binary_random, m_auth_data_length, 1, urandom_handler);
        fclose(urandom_handler);

        // First character always underscore to meet req
        // for linux env (it can't start with digit).
        nonce[0] = '_';
        for (size_t i = 1; i < m_auth_data_length - 1; i++)
        {
            nonce[i] = m_charset[m_binary_random[i] % (m_auth_data_length - 1)];
        }
        return COI_SUCCESS;
    }

    COIRESULT ValidateAuthData(const std::string &cred1, const char *cred2 = NULL)
    {
        COIRESULT result = COI_ERROR;

        if (cred1 == cred2)
        {
            result = COI_SUCCESS;
        }
        else
        {
            result = COI_AUTHENTICATION_FAILURE;
        }

        return result;
    }

    ~SSHAuth()
    {
        delete[] m_binary_random;
    }

private:
    static const char m_charset[];
    char *m_binary_random;

};

const char SSHAuth::m_charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";

static const char *m_auth_modes[] = {"invalid", "ssh", "noauth", "munge"};
const char *COISecurity::GetAuthModeName()
{
    return m_auth_modes[m_auth_mode];
}

COIRESULT COISecurity::Initialize(const char *mode_cstr)
{
    if (m_auth_mode != AUTH_UNINITIALIZED)
    {
        return COI_SUCCESS;
    }

    if (mode_cstr == NULL)
    {
        m_auth_mode = AUTH_SSH;
    }
    else
    {
        std::string mode(mode_cstr);
        bool is_mode_valid = false;
        for (unsigned i = 0; i < NUMBER_OF_AUTH_MODES; i++)
        {
            if (std::string(m_auth_modes[i]) == mode)
            {
                m_auth_mode = i;
                is_mode_valid = true;
                break;
            }
        }

        if (is_mode_valid == false)
        {
            return COI_NOT_SUPPORTED;
        }
    }

    try
    {
        GetInstance();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return COI_ERROR;
    }
    return COI_SUCCESS;

}

COISecurity &COISecurity::GetInstance()
{
    if (m_auth_mode == COISecurity::AUTH_SSH)
    {
        static SSHAuth inst;
        return inst;
    }
    else if (m_auth_mode == COISecurity::AUTH_NOAUTH)
    {
        static NoAuth inst;
        return inst;
    }
    else if (m_auth_mode == COISecurity::AUTH_MUNGE)
    {
        static MungeAuth inst;
        return inst;
    }
    else if (m_auth_mode == 0)
    {
        std::stringstream ss;
        ss << "COISecurity object " << __FUNCTION__ << " not initialized";
        throw std::logic_error(ss.str());
    }
}

