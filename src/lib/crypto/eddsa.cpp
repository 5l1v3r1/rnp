/*
 * Copyright (c) 2017, [Ribose Inc](https://www.ribose.com).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <botan/ffi.h>
#include "eddsa.h"
#include "utils.h"

rnp_result_t
eddsa_generate(rng_t *rng, pgp_ec_key_t *key, size_t numbits)
{
    botan_privkey_t eddsa = NULL;
    rnp_result_t    ret = RNP_ERROR_GENERIC;
    uint8_t         key_bits[64];

    if (numbits != 255) {
        return RNP_ERROR_BAD_PARAMETERS;
    }

    if (botan_privkey_create(&eddsa, "Ed25519", NULL, rng_handle(rng)) != 0) {
        goto end;
    }

    if (botan_privkey_ed25519_get_privkey(eddsa, key_bits)) {
        goto end;
    }

    // First 32 bytes of key_bits are the EdDSA seed (private key)
    // Second 32 bytes are the EdDSA public key

    mem2mpi(&key->x, key_bits, 32);
    // insert the required 0x40 prefix on the public key
    key_bits[31] = 0x40;
    mem2mpi(&key->p, key_bits + 31, 33);
    key->curve = PGP_CURVE_ED25519;

    ret = RNP_SUCCESS;
end:
    botan_privkey_destroy(eddsa);
    return ret;
}

rnp_result_t
eddsa_verify(const pgp_ec_signature_t *sig,
             const uint8_t *           hash,
             size_t                    hash_len,
             const pgp_ec_key_t *      key)
{
    botan_pubkey_t       eddsa = NULL;
    botan_pk_op_verify_t verify_op = NULL;
    rnp_result_t         ret = RNP_ERROR_SIGNATURE_INVALID;
    uint8_t              bn_buf[64] = {0};
    size_t               sz;

    // Check curve OID matches 25519
    if (key->curve != PGP_CURVE_ED25519) {
        ret = RNP_ERROR_BAD_PARAMETERS;
        goto done;
    }

    /*
     * See draft-ietf-openpgp-rfc4880bis-01 section 13.3
     */
    sz = mpi_bytes(&key->p);
    if ((sz != 33) || (key->p.mpi[0] != 0x40)) {
        ret = RNP_ERROR_BAD_PARAMETERS;
        goto done;
    }

    if (botan_pubkey_load_ed25519(&eddsa, key->p.mpi + 1)) {
        ret = RNP_ERROR_BAD_PARAMETERS;
        goto done;
    }

    if (botan_pk_op_verify_create(&verify_op, eddsa, "Pure", 0) != 0) {
        goto done;
    }

    if (botan_pk_op_verify_update(verify_op, hash, hash_len) != 0) {
        goto done;
    }

    // Unexpected size for Ed25519 signature
    if ((mpi_bytes(&sig->r) > 32) || (mpi_bytes(&sig->s) > 32)) {
        goto done;
    }
    mpi2mem(&sig->r, &bn_buf[32 - mpi_bytes(&sig->r)]);
    mpi2mem(&sig->s, &bn_buf[64 - mpi_bytes(&sig->s)]);

    if (botan_pk_op_verify_finish(verify_op, bn_buf, 64) == 0) {
        ret = RNP_SUCCESS;
    }
done:
    botan_pk_op_verify_destroy(verify_op);
    botan_pubkey_destroy(eddsa);
    return ret;
}

rnp_result_t
eddsa_sign(rng_t *             rng,
           pgp_ec_signature_t *sig,
           const uint8_t *     hash,
           size_t              hash_len,
           const pgp_ec_key_t *key)
{
    botan_privkey_t    eddsa = NULL;
    botan_pk_op_sign_t sign_op = NULL;
    rnp_result_t       ret = RNP_ERROR_SIGNING_FAILED;
    uint8_t            bn_buf[64] = {0};
    size_t             sz;
    size_t             sig_size = sizeof(bn_buf);

    // Check curve OID matches 25519
    if (key->curve != PGP_CURVE_ED25519) {
        goto done;
    }

    // Unexpected size for Ed25519 key
    sz = mpi_bytes(&key->x);
    if (!sz || (sz > 32)) {
        RNP_LOG("wrong key");
        ret = RNP_ERROR_BAD_PARAMETERS;
        goto done;
    }

    mpi2mem(&key->x, bn_buf + 32 - sz);

    if (botan_privkey_load_ed25519(&eddsa, bn_buf) != 0) {
        ret = RNP_ERROR_BAD_PARAMETERS;
        goto done;
    }

    if (botan_pk_op_sign_create(&sign_op, eddsa, "Pure", 0) != 0) {
        goto done;
    }

    if (botan_pk_op_sign_update(sign_op, hash, hash_len) != 0) {
        goto done;
    }

    if (botan_pk_op_sign_finish(sign_op, rng_handle(rng), bn_buf, &sig_size) != 0) {
        goto done;
    }

    // Unexpected size...
    if (sig_size != 64) {
        goto done;
    }

    mem2mpi(&sig->r, bn_buf, 32);
    mem2mpi(&sig->s, bn_buf + 32, 32);
    ret = RNP_SUCCESS;
done:
    botan_pk_op_sign_destroy(sign_op);
    botan_privkey_destroy(eddsa);
    return ret;
}