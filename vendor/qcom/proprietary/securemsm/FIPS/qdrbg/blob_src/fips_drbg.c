/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, and instead of the terms immediately above, this
 * software may be relicensed by the recipient at their option under the
 * terms of the GNU General Public License version 2 ("GPL") and only
 * version 2.  If the recipient chooses to relicense the software under
 * the GPL, then the recipient shall replace all of the text immediately
 * above and including this paragraph with the text immediately below
 * and between the words START OF ALTERNATE GPL TERMS and END OF
 * ALTERNATE GPL TERMS and such notices and license terms shall apply
 * INSTEAD OF the notices and licensing terms given above.
 *
 * START OF ALTERNATE GPL TERMS
 *
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This software was originally licensed under the Code Aurora Forum
 * Inc. Dual BSD/GPL License version 1.1 and relicensed as permitted
 * under the terms thereof by a recipient under the General Public
 * License Version 2.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * END OF ALTERNATE GPL TERMS
 *
 */
#include "../msm_rng_wrapper.h"
#include "msm_rng.h"
#include "fips_drbg.h"

/* The fips-140 random number generator is a wrapper around the CTR_DRBG
 * random number generator, which is built according to the
 * specifications in NIST SP 800-90 using AES-128.
 *
 * This wrapper has the following functionality
 * a. Entropy collection is via a callback.
 * b. A failure of CTR_DRBG because reseeding is needed invisibly
 *    causes the underlying CTR_DRBG instance to be reseeded with
 *    new random data and then the generate request is retried.
 * c. Limitations in CTR_DRBG (like not allowed more than 65536 bytes
 *    to be genrated in one request) are worked around.  At this level
 *    it just works.
 * d. On success the return value is zero.  If the callback was invoked
 *    and returned a non-zero value, that value is returned.  On all other
 *    errors -1 is returned.
 */

#ifndef NULL
  #define NULL  0
#endif

/*  32 bytes = 256 bits = seed length */
#define MAGIC 0xab10d161

#define RESEED_INTERVAL (1 << 31)

int get_entropy_callback(void *ctx, void *buf)
{
	struct msm_rng_device *msm_rng_dev = (struct msm_rng_device *)ctx;
	int ret_val = -1;

	if (NULL == ctx) {
		qdrbg_m_pr_err("%s: ctx is null\n", __func__);
		return FIPS140_PRNG_ERR;
	}

	if (NULL == buf) {
		qdrbg_m_pr_err("%s: buf is null\n", __func__);
		return FIPS140_PRNG_ERR;
	}

	ret_val = msm_rng_direct_read(msm_rng_dev, buf, Q_HW_DRBG_BLOCK_BYTES);
	if ((size_t)ret_val != Q_HW_DRBG_BLOCK_BYTES)
		return ret_val;

	return 0;
}

/* Initialize *ctx. Automatically reseed after reseed_interval calls
 * to fips_drbg_gen.  The underlying CTR_DRBG will automatically be
 * reseeded every reseed_interval requests.  Values over
 * CTR_DRBG_MAX_RESEED_INTERVAL (2^48) or that are zero are silently
 * converted to CTR_DRBG_MAX_RESEED_INTERVAL.  (It is easy to justify
 * lowering values that are too large to CTR_DRBG_MAX_RESEED_INTERVAL
 * (the NIST SP800-90 limit): just silently enforcing the rules.
 * Silently converted 0 to to CTR_DRBG_MAX_RESEED_INTERVAL is harder.
 * The alternative is to return an error.  But since
 * CTR_DRBG_MAX_RESEED is safe, we relieve the caller of one more
 * error to worry about.)
 */
static int
do_fips_drbg_init(struct fips_drbg_ctx_s *ctx,
	      get_entropy_callback_t callback,
	      void *callback_ctx,
	      unsigned long long reseed_interval)
{
	uint8_t entropy_pool[Q_HW_DRBG_BLOCK_BYTES];
	enum ctr_drbg_status_t init_rv;
	int rv = -1;

	if (ctx == NULL) {
		qdrbg_m_pr_err("%s: ctx is null\n", __func__);
		return FIPS140_PRNG_ERR;
	}
	if (callback == NULL) {
		qdrbg_m_pr_err("%s: callback is null\n", __func__);
		return FIPS140_PRNG_ERR;
	}
	if (reseed_interval == 0 ||
		reseed_interval > CTR_DRBG_MAX_RESEED_INTERVAL)
		reseed_interval = CTR_DRBG_MAX_RESEED_INTERVAL;

	mutex_init(&ctx->drbg_lock);

	/* fill in callback related fields in ctx */
	ctx->get_entropy_callback = callback;
	ctx->get_entropy_callback_ctx = callback_ctx;

	if (!ctx->fips_drbg_started) {
		rv = (*ctx->get_entropy_callback)(ctx->get_entropy_callback_ctx,
			ctx->prev_hw_drbg_block
			);
		if (rv != 0) {
			qdrbg_m_pr_err("%s: get_entropy_callback err: %d\n",
					__func__, rv);
			return FIPS140_PRNG_ERR;
		}
		ctx->fips_drbg_started = 1;
	}

	rv = (*ctx->get_entropy_callback)(ctx->get_entropy_callback_ctx,
		entropy_pool
		);
	if (rv != 0) {
		qdrbg_memset(entropy_pool, 0, Q_HW_DRBG_BLOCK_BYTES);
		qdrbg_m_pr_err("%s: get entropy_pool err: %d\n",
				 __func__, rv);
		return FIPS140_PRNG_ERR;
	}

	if (!qdrbg_memcmp(entropy_pool,
			ctx->prev_hw_drbg_block,
			Q_HW_DRBG_BLOCK_BYTES)) {
		qdrbg_memset(entropy_pool, 0, Q_HW_DRBG_BLOCK_BYTES);
		qdrbg_panic("%s: FIPS DRBG continuous test failed!\n",
				 __func__);
	} else
		qdrbg_memcpy(ctx->prev_hw_drbg_block,
			entropy_pool,
			Q_HW_DRBG_BLOCK_BYTES);


	init_rv = ctr_drbg_instantiate(&ctx->ctr_drbg_ctx,
		entropy_pool,
		8 * MSM_ENTROPY_BUFFER_SIZE,
		entropy_pool + MSM_ENTROPY_BUFFER_SIZE,
		8 * 8,
		reseed_interval);

	qdrbg_memset(entropy_pool, 0, Q_HW_DRBG_BLOCK_BYTES);

	if (init_rv == 0)
		ctx->magic = MAGIC;

	return init_rv;
}

int fips_drbg_init(struct msm_rng_device *msm_rng_ctx)
{
	uint32_t ret_val = 0;

	ret_val = do_fips_drbg_init(msm_rng_ctx->drbg_ctx,
			get_entropy_callback,
			msm_rng_ctx,
			RESEED_INTERVAL
			);
	if (ret_val != 0) {
		qdrbg_m_pr_err("%s: do_fips_drbg_init err: %d\n",
				 __func__, ret_val);
		ret_val = FIPS140_PRNG_ERR;
	}

	return ret_val;
}

/* Push new entropy into the CTR_DRBG instance in ctx, combining
 * it with the entropy already there.  On success, 0 is returned.
 * If the callback returns a non-zero value, that value is returned.
 * Other errors return -1.
 */
static int
fips_drbg_reseed(struct fips_drbg_ctx_s *ctx)
{
	uint8_t entropy_pool[Q_HW_DRBG_BLOCK_BYTES];
	int rv;
	enum ctr_drbg_status_t init_rv;

	if (ctx == NULL) {
		qdrbg_m_pr_err("%s: ctx is null\n", __func__);
		return FIPS140_PRNG_ERR;
	}

	if (!ctx->fips_drbg_started) {
		rv = (*ctx->get_entropy_callback)(ctx->get_entropy_callback_ctx,
			ctx->prev_hw_drbg_block
			);
		if (rv != 0) {
			qdrbg_m_pr_err("%s: get_entropy_callback err: %d\n",
					__func__, rv);
			return FIPS140_PRNG_ERR;
		}
		ctx->fips_drbg_started = 1;
	}

	rv = (*ctx->get_entropy_callback)(ctx->get_entropy_callback_ctx,
		entropy_pool
		);
	if (rv != 0) {
		qdrbg_memset(entropy_pool, 0, Q_HW_DRBG_BLOCK_BYTES);
		qdrbg_m_pr_err("%s: get entropy_pool err: %d\n",
				 __func__, rv);
		return FIPS140_PRNG_ERR;
	}

	if (!qdrbg_memcmp(entropy_pool,
		    ctx->prev_hw_drbg_block,
		    Q_HW_DRBG_BLOCK_BYTES)) {
		qdrbg_memset(entropy_pool, 0, Q_HW_DRBG_BLOCK_BYTES);
		qdrbg_panic("%s: FIPS DRBG continuous test failed!\n",
				 __func__);
	} else
		qdrbg_memcpy(ctx->prev_hw_drbg_block,
				entropy_pool,
				Q_HW_DRBG_BLOCK_BYTES);

	init_rv = ctr_drbg_reseed(&ctx->ctr_drbg_ctx,
				  entropy_pool,
				  8 * MSM_ENTROPY_BUFFER_SIZE);

	/* Zeroize the buffer for security. */
	qdrbg_memset(entropy_pool, 0, Q_HW_DRBG_BLOCK_BYTES);

	if (CTR_DRBG_SUCCESS != init_rv) {
		qdrbg_m_pr_err("%s: ctr_drbg_reseed failed: %d\n",
				 __func__, init_rv);
	}

	return init_rv == CTR_DRBG_SUCCESS ?
				FIPS140_PRNG_OK :
				FIPS140_PRNG_ERR;
}

/* generate random bytes.  len is in bytes On success returns 0.  If
 * the callback returns a non-zero value, that is returned.  Other
 * errors return -1. */
int
fips_drbg_gen(struct fips_drbg_ctx_s *ctx, void *tgt, size_t len)
{

	/* The contorted flow in this function is so that the CTR_DRBG
	stuff can follow NIST SP 800-90, which has the generate function
	fail and return a special code if a reseed is needed. We also work
	around the CTR_DRBG limitation of the maximum request sized being
	2^19 bits. */

	enum ctr_drbg_status_t gen_rv;
	int rv=FIPS140_PRNG_ERR;

	if (ctx == NULL || ctx->magic != MAGIC) {
		qdrbg_m_pr_err("%s: invalid argument: ctx=%p\n",
				__func__, ctx);
		if (ctx) {
			qdrbg_m_pr_err("%s: invalid argument: magic=%x\n",
					__func__, ctx->magic);
		}
		return FIPS140_PRNG_ERR;
	}
	if (tgt == NULL && len > 0) {
		qdrbg_m_pr_err("%s: invalid argument: buf=%p, len=%d\n",
				__func__, tgt, len);
		return FIPS140_PRNG_ERR;
	}
	mutex_lock(&ctx->drbg_lock);
	while (len > 0) {
		size_t req_len;

		if (len < (CTR_DRBG_MAX_REQ_LEN_BITS / 8))
			req_len = len;
		else
			req_len = CTR_DRBG_MAX_REQ_LEN_BITS / 8;

		gen_rv = ctr_drbg_generate(&ctx->ctr_drbg_ctx,
						tgt,
						8*req_len);
		switch (gen_rv) {
		case CTR_DRBG_SUCCESS:
			tgt = (uint8_t *)tgt + req_len;
			len -= req_len;
			break;
		case CTR_DRBG_NEEDS_RESEED:
			rv = fips_drbg_reseed(ctx);
			if (FIPS140_PRNG_OK != rv)
				goto err;
			break;
		default:
			qdrbg_m_pr_err("%s: ctr_drbg_generate err: %d\n",
					__func__, gen_rv);
			goto err;
		}
	}
	rv = FIPS140_PRNG_OK;
err:
	mutex_unlock(&ctx->drbg_lock);
	return rv;
}

/* free resources and zeroize state */
void
fips_drbg_final(struct fips_drbg_ctx_s *ctx)
{
	mutex_lock(&ctx->drbg_lock);
	ctr_drbg_uninstantiate(&ctx->ctr_drbg_ctx);
	ctx->get_entropy_callback     = 0;
	ctx->get_entropy_callback_ctx = 0;
	ctx->fips_drbg_started        = 0;
	qdrbg_memset(ctx->prev_hw_drbg_block, 0, Q_HW_DRBG_BLOCK_BYTES);
	ctx->magic = 0;
	mutex_unlock(&ctx->drbg_lock);
}

