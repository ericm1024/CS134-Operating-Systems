// Eric Mueller -- emueller@hmc.edu -- 40160869
//
// Common functions
#include "common.h"

void die(const char *reason, int err)
{
        if (reason) {
                errno = err;
                perror(reason);
                exit(1);
        } else {
                exit(0);
        }
}

MCRYPT encryption_setup(const char *key_fname)
{
        // AES-128 in stream mode
        MCRYPT td = mcrypt_module_open("rijndael-128", NULL, "cfb", NULL);
        if (td == MCRYPT_FAILED)
                die("failed to open mcrypt module", EINVAL);

        // set up the key: open the key file, make sure it is the right
        // length, and read it into a buffer
        int key_fd = open(key_fname, O_RDONLY);
        if (key_fd < 0)
                die("failed to open key file", errno);

        struct stat st;
        int err = fstat(key_fd, &st);
        if (err)
                die("fstat failed", errno);

        // this is technically the *maximum* key size supported by the alg,
        // but we're just gonna use that for simplicity
        int key_size = mcrypt_enc_get_key_size(td);
        if (st.st_size != key_size) {
                fprintf(stderr, "impropperly sized key: expected %d bytes, got %ld bytes\n",
                        key_size, st.st_size);
                exit(1);
        }

        // malloc a buffer for the key
        void *key = malloc(key_size);
        if (!key)
                die("malloc", ENOMEM);

        // read the key into the buffer
        err = read(key_fd, key, key_size);
        if (err != key_size)
                die("failed to read key file", errno);

        // malloc an iv for the algorithm
        void *iv = malloc(mcrypt_enc_get_iv_size(td));
        if (!iv)
                die("malloc", ENOMEM);

        // set up the IV
        // 
        // NB: the man page for mcrypt says the iv "needs to be random and
        // unique (but not secret)" and that "The same IV must be used for
        // encryption/decryption."
        //
        // This is a pain in the ass for our purposes: it means our client
        // and server would need to communicate the iv at the start of every
        // session. We're going to be lazy and set it to all zeros.
        memset(iv, 0, mcrypt_enc_get_iv_size(td));

        // finally, call mcrypt_generic_init
        err = mcrypt_generic_init(td, key, key_size, iv);
        if (err)
                die("mcrypt_generic_init", EINVAL);

        // presumably mcrypt copies these buffers and doesn't steal them, so
        // we're safe to free them, but the API doesn't seem to specify one
        // way or another
        free(iv);
        free(key);
        close(key_fd);

        return td;
}
