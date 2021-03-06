/*
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  */
#pragma once

#include "cryptonight.h"
#include <memory.h>
#include <stdio.h>
#include <float.h>
#include <cfenv>

#ifdef __GNUC__
#include <x86intrin.h>
static inline uint64_t _umul128(uint64_t a, uint64_t b, uint64_t* hi)
{
	unsigned __int128 r = (unsigned __int128)a * (unsigned __int128)b;
	*hi = r >> 64;
	return (uint64_t)r;
}
#define _mm256_set_m128i(v0, v1)  _mm256_insertf128_si256(_mm256_castsi128_si256(v1), (v0), 1)
#else
#include <intrin.h>
#endif // __GNUC__

#if !defined(_LP64) && !defined(_WIN64)
#error You are trying to do a 32-bit build. This will all end in tears. I know it.
#endif

extern "C"
{
	void keccak(const uint8_t *in, int inlen, uint8_t *md, int mdlen);
	void keccakf(uint64_t st[25], int rounds);
	extern void(*const extra_hashes[4])(const void *, size_t, char *);

	__m128i soft_aesenc(__m128i in, __m128i key);
	__m128i soft_aeskeygenassist(__m128i key, uint8_t rcon);
}

// This will shift and xor tmp1 into itself as 4 32-bit vals such as
// sl_xor(a1 a2 a3 a4) = a1 (a2^a1) (a3^a2^a1) (a4^a3^a2^a1)
static inline __m128i sl_xor(__m128i tmp1)
{
	__m128i tmp4;
	tmp4 = _mm_slli_si128(tmp1, 0x04);
	tmp1 = _mm_xor_si128(tmp1, tmp4);
	tmp4 = _mm_slli_si128(tmp4, 0x04);
	tmp1 = _mm_xor_si128(tmp1, tmp4);
	tmp4 = _mm_slli_si128(tmp4, 0x04);
	tmp1 = _mm_xor_si128(tmp1, tmp4);
	return tmp1;
}

template<uint8_t rcon>
static inline void aes_genkey_sub(__m128i* xout0, __m128i* xout2)
{
	__m128i xout1 = _mm_aeskeygenassist_si128(*xout2, rcon);
	xout1 = _mm_shuffle_epi32(xout1, 0xFF); // see PSHUFD, set all elems to 4th elem
	*xout0 = sl_xor(*xout0);
	*xout0 = _mm_xor_si128(*xout0, xout1);
	xout1 = _mm_aeskeygenassist_si128(*xout0, 0x00);
	xout1 = _mm_shuffle_epi32(xout1, 0xAA); // see PSHUFD, set all elems to 3rd elem
	*xout2 = sl_xor(*xout2);
	*xout2 = _mm_xor_si128(*xout2, xout1);
}

static inline void soft_aes_genkey_sub(__m128i* xout0, __m128i* xout2, uint8_t rcon)
{
	__m128i xout1 = soft_aeskeygenassist(*xout2, rcon);
	xout1 = _mm_shuffle_epi32(xout1, 0xFF); // see PSHUFD, set all elems to 4th elem
	*xout0 = sl_xor(*xout0);
	*xout0 = _mm_xor_si128(*xout0, xout1);
	xout1 = soft_aeskeygenassist(*xout0, 0x00);
	xout1 = _mm_shuffle_epi32(xout1, 0xAA); // see PSHUFD, set all elems to 3rd elem
	*xout2 = sl_xor(*xout2);
	*xout2 = _mm_xor_si128(*xout2, xout1);
}

template<bool SOFT_AES>
static inline void aes_genkey(const __m128i* memory, __m128i* k0, __m128i* k1, __m128i* k2, __m128i* k3,
	__m128i* k4, __m128i* k5, __m128i* k6, __m128i* k7, __m128i* k8, __m128i* k9)
{
	__m128i xout0, xout2;

	xout0 = _mm_load_si128(memory);
	xout2 = _mm_load_si128(memory+1);
	*k0 = xout0;
	*k1 = xout2;

	if(SOFT_AES)
		soft_aes_genkey_sub(&xout0, &xout2, 0x01);
	else
		aes_genkey_sub<0x01>(&xout0, &xout2);
	*k2 = xout0;
	*k3 = xout2;

	if(SOFT_AES)
		soft_aes_genkey_sub(&xout0, &xout2, 0x02);
	else
		aes_genkey_sub<0x02>(&xout0, &xout2);
	*k4 = xout0;
	*k5 = xout2;

	if(SOFT_AES)
		soft_aes_genkey_sub(&xout0, &xout2, 0x04);
	else
		aes_genkey_sub<0x04>(&xout0, &xout2);
	*k6 = xout0;
	*k7 = xout2;

	if(SOFT_AES)
		soft_aes_genkey_sub(&xout0, &xout2, 0x08);
	else
		aes_genkey_sub<0x08>(&xout0, &xout2);
	*k8 = xout0;
	*k9 = xout2;
}

static inline void aes_round(__m128i key, __m128i* x0, __m128i* x1, __m128i* x2, __m128i* x3, __m128i* x4, __m128i* x5, __m128i* x6, __m128i* x7)
{
	*x0 = _mm_aesenc_si128(*x0, key);
	*x1 = _mm_aesenc_si128(*x1, key);
	*x2 = _mm_aesenc_si128(*x2, key);
	*x3 = _mm_aesenc_si128(*x3, key);
	*x4 = _mm_aesenc_si128(*x4, key);
	*x5 = _mm_aesenc_si128(*x5, key);
	*x6 = _mm_aesenc_si128(*x6, key);
	*x7 = _mm_aesenc_si128(*x7, key);
}

static inline void soft_aes_round(__m128i key, __m128i* x0, __m128i* x1, __m128i* x2, __m128i* x3, __m128i* x4, __m128i* x5, __m128i* x6, __m128i* x7)
{
	*x0 = soft_aesenc(*x0, key);
	*x1 = soft_aesenc(*x1, key);
	*x2 = soft_aesenc(*x2, key);
	*x3 = soft_aesenc(*x3, key);
	*x4 = soft_aesenc(*x4, key);
	*x5 = soft_aesenc(*x5, key);
	*x6 = soft_aesenc(*x6, key);
	*x7 = soft_aesenc(*x7, key);
}

template<size_t MEM, bool SOFT_AES, bool PREFETCH>
void cn_explode_scratchpad(const __m128i* input, __m128i* output)
{
	// This is more than we have registers, compiler will assign 2 keys on the stack
	__m128i xin0, xin1, xin2, xin3, xin4, xin5, xin6, xin7;
	__m128i k0, k1, k2, k3, k4, k5, k6, k7, k8, k9;

	aes_genkey<SOFT_AES>(input, &k0, &k1, &k2, &k3, &k4, &k5, &k6, &k7, &k8, &k9);

	xin0 = _mm_load_si128(input + 4);
	xin1 = _mm_load_si128(input + 5);
	xin2 = _mm_load_si128(input + 6);
	xin3 = _mm_load_si128(input + 7);
	xin4 = _mm_load_si128(input + 8);
	xin5 = _mm_load_si128(input + 9);
	xin6 = _mm_load_si128(input + 10);
	xin7 = _mm_load_si128(input + 11);

	for (size_t i = 0; i < MEM / sizeof(__m128i); i += 8)
	{
		if(SOFT_AES)
		{
			soft_aes_round(k0, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k1, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k2, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k3, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k4, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k5, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k6, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k7, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k8, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			soft_aes_round(k9, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
		}
		else
		{
			aes_round(k0, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k1, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k2, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k3, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k4, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k5, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k6, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k7, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k8, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
			aes_round(k9, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
		}

		_mm_store_si128(output + i + 0, xin0);
		_mm_store_si128(output + i + 1, xin1);
		_mm_store_si128(output + i + 2, xin2);
		_mm_store_si128(output + i + 3, xin3);

		if(PREFETCH)
			_mm_prefetch((const char*)output + i + 0, _MM_HINT_T2);

		_mm_store_si128(output + i + 4, xin4);
		_mm_store_si128(output + i + 5, xin5);
		_mm_store_si128(output + i + 6, xin6);
		_mm_store_si128(output + i + 7, xin7);

		if(PREFETCH)
			_mm_prefetch((const char*)output + i + 4, _MM_HINT_T2);
	}
}

template<size_t MEM, bool SOFT_AES, bool PREFETCH>
void cn_implode_scratchpad(const __m128i* input, __m128i* output)
{
	// This is more than we have registers, compiler will assign 2 keys on the stack
	__m128i xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7;
	__m128i k0, k1, k2, k3, k4, k5, k6, k7, k8, k9;

	aes_genkey<SOFT_AES>(output + 2, &k0, &k1, &k2, &k3, &k4, &k5, &k6, &k7, &k8, &k9);

	xout0 = _mm_load_si128(output + 4);
	xout1 = _mm_load_si128(output + 5);
	xout2 = _mm_load_si128(output + 6);
	xout3 = _mm_load_si128(output + 7);
	xout4 = _mm_load_si128(output + 8);
	xout5 = _mm_load_si128(output + 9);
	xout6 = _mm_load_si128(output + 10);
	xout7 = _mm_load_si128(output + 11);

	for (size_t i = 0; i < MEM / sizeof(__m128i); i += 8)
	{
		if(PREFETCH)
			_mm_prefetch((const char*)input + i + 0, _MM_HINT_NTA);

		xout0 = _mm_xor_si128(_mm_load_si128(input + i + 0), xout0);
		xout1 = _mm_xor_si128(_mm_load_si128(input + i + 1), xout1);
		xout2 = _mm_xor_si128(_mm_load_si128(input + i + 2), xout2);
		xout3 = _mm_xor_si128(_mm_load_si128(input + i + 3), xout3);

		if(PREFETCH)
			_mm_prefetch((const char*)input + i + 4, _MM_HINT_NTA);

		xout4 = _mm_xor_si128(_mm_load_si128(input + i + 4), xout4);
		xout5 = _mm_xor_si128(_mm_load_si128(input + i + 5), xout5);
		xout6 = _mm_xor_si128(_mm_load_si128(input + i + 6), xout6);
		xout7 = _mm_xor_si128(_mm_load_si128(input + i + 7), xout7);

		if(SOFT_AES)
		{
			soft_aes_round(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
		}
		else
		{
			aes_round(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
		}
	}

	_mm_store_si128(output + 4, xout0);
	_mm_store_si128(output + 5, xout1);
	_mm_store_si128(output + 6, xout2);
	_mm_store_si128(output + 7, xout3);
	_mm_store_si128(output + 8, xout4);
	_mm_store_si128(output + 9, xout5);
	_mm_store_si128(output + 10, xout6);
	_mm_store_si128(output + 11, xout7);
}

#ifdef __GNUC__
#define LIKELY(X) __builtin_expect(X, 1)
#define UNLIKELY(X) __builtin_expect(X, 0)
#define FORCEINLINE __attribute__((always_inline)) inline
#else
#define LIKELY(X) X
#define UNLIKELY(X) X
#define FORCEINLINE __forceinline
#endif

template<bool IS_PGO>
static FORCEINLINE void int_sqrt_v2_fixup(uint64_t& r, uint64_t n0)
{
	// This works well only with profile guided optimizations
	if (IS_PGO)
	{
		// _mm_sqrt_sd has 52 bits of precision while we need only 33 bits
		// It's very likely that fix up step is not needed
		if (LIKELY(r & 524287))
		{
			r >>= 19;
			return;
		}

		// The execution gets here only when r ends with 19 zero bits
		// One would expect it to happen in 1 of 524,288 iterations (once per hash)
		// but the actual number is 1 of ~470,000 iterations (~1.1155 times per hash)
		// due to non-linearity of the square root function
		--r;
	}

	const uint64_t s = r >> 20;
	r >>= 19;

	uint64_t x2 = (s - (1022ULL << 32)) * (r - s - (1022ULL << 32) + 1);
#if (defined(_MSC_VER) || __GNUC__ > 7 || (__GNUC__ == 7 && __GNUC_MINOR__ > 1)) && (defined(__x86_64__) || defined(_M_AMD64))
	_addcarry_u64(_subborrow_u64(0, x2, n0, (unsigned long long int*)&x2), r, 0, (unsigned long long int*)&r);
#else
	// GCC versions prior to 7 don't generate correct assembly for _subborrow_u64 -> _addcarry_u64 sequence
	// Fallback to simpler code
	if (x2 < n0) ++r;
#endif
}

static FORCEINLINE uint64_t int_sqrt_v2(uint64_t n0)
{
	__m128d x = _mm_castsi128_pd(_mm_add_epi64(_mm_cvtsi64_si128(n0 >> 12), _mm_set_epi64x(0, 1023ULL << 52)));
	x = _mm_sqrt_sd(_mm_setzero_pd(), x);
	uint64_t r = static_cast<uint64_t>(_mm_cvtsi128_si64(_mm_castpd_si128(x)));
	int_sqrt_v2_fixup<
#ifdef PGO_BUILD
		true
#else
		false
#endif
	>(r, n0);
	return r;
}

template<size_t ITERATIONS, size_t MEM, bool SOFT_AES, bool PREFETCH, bool SHUFFLE, bool INT_MATH>
void cryptonight_hash(const void* input, size_t len, void* output, cryptonight_ctx* ctx0)
{
	keccak((const uint8_t *)input, len, ctx0->hash_state, 200);

	// Optim - 99% time boundary
	cn_explode_scratchpad<MEM, SOFT_AES, PREFETCH>((__m128i*)ctx0->hash_state, (__m128i*)ctx0->long_state);

	uint8_t* l0 = ctx0->long_state;
	uint64_t* h0 = (uint64_t*)ctx0->hash_state;

	uint64_t al0 = h0[0] ^ h0[4];
	uint64_t ah0 = h0[1] ^ h0[5];
	__m128i bx0 = _mm_set_epi64x(h0[3] ^ h0[7], h0[2] ^ h0[6]);
	__m128i bx1 = _mm_set_epi64x(h0[9] ^ h0[11], h0[8] ^ h0[10]);

	uint64_t idx0 = h0[0] ^ h0[4];
	uint64_t idx1 = idx0 & 0x1FFFF0;

	__m128i division_result_xmm = _mm_cvtsi64_si128(h0[12]);
	uint64_t sqrt_result = h0[13];

#ifdef PGO_BUILD
#ifdef _MSC_VER
	_control87(RC_UP, MCW_RC);
#else
	std::fesetround(FE_UPWARD);
#endif
#else
#ifdef _MSC_VER
	_control87(RC_DOWN, MCW_RC);
#else
	std::fesetround(FE_TOWARDZERO);
#endif
#endif

	// Optim - 90% time boundary
	for(size_t i = 0; i < ITERATIONS; i++)
	{
		__m128i cx;
		cx = _mm_load_si128((__m128i *)&l0[idx1]);

		const __m128i ax0 = _mm_set_epi64x(ah0, al0);
		if(SOFT_AES)
			cx = soft_aesenc(cx, ax0);
		else
			cx = _mm_aesenc_si128(cx, ax0);

		if (SHUFFLE)
		{
			const __m128i chunk1 = _mm_load_si128((__m128i *)&l0[idx1 ^ 0x10]);
			const __m128i chunk2 = _mm_load_si128((__m128i *)&l0[idx1 ^ 0x20]);
			const __m128i chunk3 = _mm_load_si128((__m128i *)&l0[idx1 ^ 0x30]);
			_mm_store_si128((__m128i *)&l0[idx1 ^ 0x10], _mm_add_epi64(chunk3, bx1));
			_mm_store_si128((__m128i *)&l0[idx1 ^ 0x20], _mm_add_epi64(chunk1, bx0));
			_mm_store_si128((__m128i *)&l0[idx1 ^ 0x30], _mm_add_epi64(chunk2, ax0));
		}

		_mm_store_si128((__m128i *)&l0[idx1], _mm_xor_si128(bx0, cx));
		idx0 = _mm_cvtsi128_si64(cx);
		idx1 = idx0 & 0x1FFFF0;

		if(PREFETCH)
			_mm_prefetch((const char*)&l0[idx1], _MM_HINT_T0);

		uint64_t hi, lo, cl, ch;
		cl = ((uint64_t*)&l0[idx1])[0];
		ch = ((uint64_t*)&l0[idx1])[1];

		if (INT_MATH)
		{
			// Use division and square root results from the _previous_ iteration to hide the latency
			const uint64_t cx0 = _mm_cvtsi128_si64(cx);
			cl ^= static_cast<uint64_t>(_mm_cvtsi128_si64(division_result_xmm)) ^ (sqrt_result << 32);
			const uint32_t d = (cx0 + (sqrt_result << 1)) | 0x80000001UL;

			// Most and least significant bits in the divisor are set to 1
			// to make sure we don't divide by a small or even number,
			// so there are no shortcuts for such cases
			//
			// Quotient may be as large as (2^64 - 1)/(2^31 + 1) = 8589934588 = 2^33 - 4
			// We drop the highest bit to fit both quotient and remainder in 32 bits

			// Compiler will optimize it to a single div instruction
			const uint64_t cx1 = _mm_cvtsi128_si64(_mm_srli_si128(cx, 8));
			const uint64_t division_result = static_cast<uint32_t>(cx1 / d) + ((cx1 % d) << 32);
			division_result_xmm = _mm_cvtsi64_si128(static_cast<int64_t>(division_result));

			// Use division_result as an input for the square root to prevent parallel implementation in hardware
			sqrt_result = int_sqrt_v2(cx0 + division_result);
		}

		lo = _umul128(idx0, cl, &hi);

		// Shuffle the other 3x16 byte chunks in the current 64-byte cache line
		if (SHUFFLE)
		{
			// Shuffle constants here were chosen carefully
			// to maximize permutation cycle length
			// and have no 2-byte elements stay in their places
			const __m128i chunk1 = _mm_load_si128((__m128i *)&l0[idx1 ^ 0x10]);
			const __m128i chunk2 = _mm_load_si128((__m128i *)&l0[idx1 ^ 0x20]);
			const __m128i chunk3 = _mm_load_si128((__m128i *)&l0[idx1 ^ 0x30]);
			_mm_store_si128((__m128i *)&l0[idx1 ^ 0x10], _mm_add_epi64(chunk3, bx1));
			_mm_store_si128((__m128i *)&l0[idx1 ^ 0x20], _mm_add_epi64(chunk1, bx0));
			_mm_store_si128((__m128i *)&l0[idx1 ^ 0x30], _mm_add_epi64(chunk2, ax0));
		}

		al0 += hi;
		ah0 += lo;
		((uint64_t*)&l0[idx1])[0] = al0;
		((uint64_t*)&l0[idx1])[1] = ah0;
		ah0 ^= ch;
		al0 ^= cl;
		idx0 = al0;
		idx1 = idx0 & 0x1FFFF0;

		if(PREFETCH)
			_mm_prefetch((const char*)&l0[idx1], _MM_HINT_T0);

		if (SHUFFLE)
		{
			bx1 = bx0;
		}
		bx0 = cx;
	}

	// Optim - 90% time boundary
	cn_implode_scratchpad<MEM, SOFT_AES, PREFETCH>((__m128i*)ctx0->long_state, (__m128i*)ctx0->hash_state);

	// Optim - 99% time boundary

	keccakf((uint64_t*)ctx0->hash_state, 24);
	extra_hashes[ctx0->hash_state[0] & 3](ctx0->hash_state, 200, (char*)output);
}

static FORCEINLINE void int_math_v2_double_hash(__m128i& division_result, __m128i& sqrt_result, __m128i cx0, __m128i cx1)
{
	const __m128i sqrt_result2 = _mm_add_epi64(_mm_slli_epi64(sqrt_result, 1), _mm_unpacklo_epi64(cx0, cx1));
	const uint32_t d0 = _mm_cvtsi128_si64(sqrt_result2) | 0x80000001UL;
	const uint32_t d1 = _mm_cvtsi128_si64(_mm_srli_si128(sqrt_result2, 8)) | 0x80000001UL;

	const uint64_t cx01 = _mm_cvtsi128_si64(_mm_srli_si128(cx0, 8));
	const uint64_t cx11 = _mm_cvtsi128_si64(_mm_srli_si128(cx1, 8));
	__m128d x = _mm_unpacklo_pd(_mm_cvtsi64_sd(_mm_setzero_pd(), (cx01 + 1) >> 1), _mm_cvtsi64_sd(_mm_setzero_pd(), (cx11 + 1) >> 1));
	__m128d y = _mm_unpacklo_pd(_mm_cvtsi64_sd(_mm_setzero_pd(), d0), _mm_cvtsi64_sd(_mm_setzero_pd(), d1));

	__m128d result = _mm_div_pd(x, y);
	//result = _mm_add_pd(result, result);
	result = _mm_castsi128_pd(_mm_add_epi64(_mm_castpd_si128(result), _mm_set_epi64x(1ULL << 52, 1ULL << 52)));

	uint64_t q0 = _mm_cvttsd_si64(result);
	uint64_t q1 = _mm_cvttsd_si64(_mm_castsi128_pd(_mm_srli_si128(_mm_castpd_si128(result), 8)));

	uint64_t r0 = cx01 - d0 * q0;
	if (UNLIKELY(int64_t(r0) < 0))
	{
		--q0;
		r0 += d0;
	}
	uint64_t r1 = cx11 - d1 * q1;
	if (UNLIKELY(int64_t(r1) < 0))
	{
		--q1;
		r1 += d1;
	}

	division_result = _mm_set_epi32(r1, q1, r0, q0);

	__m128i sqrt_input = _mm_add_epi64(_mm_unpacklo_epi64(cx0, cx1), division_result);
	x = _mm_castsi128_pd(_mm_add_epi64(_mm_srli_epi64(sqrt_input, 12), _mm_set_epi64x(1023ULL << 52, 1023ULL << 52)));

	x = _mm_sqrt_pd(x);

	r0 = static_cast<uint64_t>(_mm_cvtsi128_si64(_mm_castpd_si128(x)));
	int_sqrt_v2_fixup<true>(r0, _mm_cvtsi128_si64(sqrt_input));
	r1 = static_cast<uint64_t>(_mm_cvtsi128_si64(_mm_srli_si128(_mm_castpd_si128(x), 8)));
	int_sqrt_v2_fixup<true>(r1, _mm_cvtsi128_si64(_mm_srli_si128(sqrt_input, 8)));
	sqrt_result = _mm_set_epi64x(r1, r0);
}

//extern uint64_t t1, t2;

template<size_t ITERATIONS, size_t MEM, bool SOFT_AES, bool PREFETCH, bool SHUFFLE, bool INT_MATH>
void cryptonight_double_hash(const void* input1, size_t len1, void* output1, const void* input2, size_t len2, void* output2, cryptonight_ctx* __restrict ctx0, cryptonight_ctx* __restrict ctx1)
{
	keccak((const uint8_t *)input1, len1, ctx0->hash_state, 200);
	keccak((const uint8_t *)input2, len2, ctx1->hash_state, 200);

	// Optim - 99% time boundary
	cn_explode_scratchpad<MEM, SOFT_AES, PREFETCH>((__m128i*)ctx0->hash_state, (__m128i*)ctx0->long_state);
	cn_explode_scratchpad<MEM, SOFT_AES, PREFETCH>((__m128i*)ctx1->hash_state, (__m128i*)ctx1->long_state);

	uint8_t* l0 = ctx0->long_state;
	uint64_t* h0 = (uint64_t*)ctx0->hash_state;
	uint8_t* l1 = ctx1->long_state;
	uint64_t* h1 = (uint64_t*)ctx1->hash_state;

	uint64_t axl0 = h0[0] ^ h0[4];
	uint64_t axh0 = h0[1] ^ h0[5];
	__m128i bx00 = _mm_set_epi64x(h0[3] ^ h0[7], h0[2] ^ h0[6]);
	__m128i bx01 = _mm_set_epi64x(h0[9] ^ h0[11], h0[8] ^ h0[10]);
	uint64_t axl1 = h1[0] ^ h1[4];
	uint64_t axh1 = h1[1] ^ h1[5];
	__m128i bx10 = _mm_set_epi64x(h1[3] ^ h1[7], h1[2] ^ h1[6]);
	__m128i bx11 = _mm_set_epi64x(h1[9] ^ h1[11], h1[8] ^ h1[10]);

	uint64_t idx00 = h0[0] ^ h0[4];
	uint64_t idx10 = h1[0] ^ h1[4];
	uint64_t idx01 = idx00 & 0x1FFFF0;
	uint64_t idx11 = idx10 & 0x1FFFF0;

	__m128i division_result_xmm = _mm_unpacklo_epi64(_mm_cvtsi64_si128(h0[12]), _mm_cvtsi64_si128(h1[12]));
	__m128i sqrt_result_xmm = _mm_unpacklo_epi64(_mm_cvtsi64_si128(h0[13]), _mm_cvtsi64_si128(h1[13]));

#ifdef _MSC_VER
	_control87(RC_UP, MCW_RC);
#else
	std::fesetround(FE_UPWARD);
#endif

	//t1 = __rdtsc();

	// Optim - 90% time boundary
	for (size_t i = 0; i < ITERATIONS; i++)
	{
		__m128i cx0 = _mm_load_si128((__m128i *)&l0[idx01]);
		const __m128i ax0 = _mm_set_epi64x(axh0, axl0);
		if (SOFT_AES)
		{
			cx0 = soft_aesenc(cx0, ax0);
		}
		else
		{
			cx0 = _mm_aesenc_si128(cx0, ax0);
		}

		if (SHUFFLE)
		{
			uint32_t k = idx01 ^ 0x10;
			const __m128i chunk1 = _mm_load_si128((__m128i *)&l0[k]); k ^= 0x30;
			const __m128i chunk2 = _mm_load_si128((__m128i *)&l0[k]);
			_mm_store_si128((__m128i *)&l0[k], _mm_add_epi64(chunk1, bx00)); k ^= 0x10;
			const __m128i chunk3 = _mm_load_si128((__m128i *)&l0[k]);
			_mm_store_si128((__m128i *)&l0[k], _mm_add_epi64(chunk2, ax0)); k ^= 0x20;
			_mm_store_si128((__m128i *)&l0[k], _mm_add_epi64(chunk3, bx01));
		}

		_mm_store_si128((__m128i *)&l0[idx01], _mm_xor_si128(bx00, cx0));
		idx00 = _mm_cvtsi128_si64(cx0);
		idx01 = idx00 & 0x1FFFF0;

		if(PREFETCH)
			_mm_prefetch((const char*)&l0[idx01], _MM_HINT_T0);

		__m128i cx1 = _mm_load_si128((__m128i *)&l1[idx11]);
		const __m128i ax1 = _mm_set_epi64x(axh1, axl1);
		if (SOFT_AES)
		{
			cx1 = soft_aesenc(cx1, ax1);
		}
		else
		{
			cx1 = _mm_aesenc_si128(cx1, ax1);
		}

		if (SHUFFLE)
		{
			uint32_t k = idx11 ^ 0x10;
			const __m128i chunk1 = _mm_load_si128((__m128i *)&l1[k]); k ^= 0x30;
			const __m128i chunk2 = _mm_load_si128((__m128i *)&l1[k]);
			_mm_store_si128((__m128i *)&l1[k], _mm_add_epi64(chunk1, bx10)); k ^= 0x10;
			const __m128i chunk3 = _mm_load_si128((__m128i *)&l1[k]);
			_mm_store_si128((__m128i *)&l1[k], _mm_add_epi64(chunk2, ax1)); k ^= 0x20;
			_mm_store_si128((__m128i *)&l1[k], _mm_add_epi64(chunk3, bx11));
		}

		_mm_store_si128((__m128i *)&l1[idx11], _mm_xor_si128(bx10, cx1));
		idx10 = _mm_cvtsi128_si64(cx1);
		idx11 = idx10 & 0x1FFFF0;

		if(PREFETCH)
			_mm_prefetch((const char*)&l1[idx11], _MM_HINT_T0);

		uint64_t hi, lo, cl, ch;
		cl = ((uint64_t*)&l0[idx01])[0];
		ch = ((uint64_t*)&l0[idx01])[1];

		if (SHUFFLE)
		{
			uint32_t k = idx01 ^ 0x10;
			const __m128i chunk1 = _mm_load_si128((__m128i *)&l0[k]); k ^= 0x30;
			const __m128i chunk2 = _mm_load_si128((__m128i *)&l0[k]);
			_mm_store_si128((__m128i *)&l0[k], _mm_add_epi64(chunk1, bx00)); k ^= 0x10;
			const __m128i chunk3 = _mm_load_si128((__m128i *)&l0[k]);
			_mm_store_si128((__m128i *)&l0[k], _mm_add_epi64(chunk2, ax0)); k ^= 0x20;
			_mm_store_si128((__m128i *)&l0[k], _mm_add_epi64(chunk3, bx01));
		}

		if (INT_MATH)
		{
			const uint64_t sqrt_result0 = _mm_cvtsi128_si64(sqrt_result_xmm);
			cl ^= static_cast<uint64_t>(_mm_cvtsi128_si64(division_result_xmm)) ^ (sqrt_result0 << 32);
		}

		lo = _umul128(idx00, cl, &hi);

		axl0 += hi;
		axh0 += lo;
		((uint64_t*)&l0[idx01])[0] = axl0;
		((uint64_t*)&l0[idx01])[1] = axh0;
		axh0 ^= ch;
		axl0 ^= cl;
		idx00 = axl0;
		idx01 = idx00 & 0x1FFFF0;

		if(PREFETCH)
			_mm_prefetch((const char*)&l0[idx01], _MM_HINT_T0);

		cl = ((uint64_t*)&l1[idx11])[0];
		ch = ((uint64_t*)&l1[idx11])[1];

		if (INT_MATH)
		{
			const uint64_t sqrt_result1 = _mm_cvtsi128_si64(_mm_srli_si128(sqrt_result_xmm, 8));
			cl ^= static_cast<uint64_t>(_mm_cvtsi128_si64(_mm_srli_si128(division_result_xmm, 8))) ^ (sqrt_result1 << 32);
			int_math_v2_double_hash(division_result_xmm, sqrt_result_xmm, cx0, cx1);
		}

		lo = _umul128(idx10, cl, &hi);

		if (SHUFFLE)
		{
			uint32_t k = idx11 ^ 0x10;
			const __m128i chunk1 = _mm_load_si128((__m128i *)&l1[k]); k ^= 0x30;
			const __m128i chunk2 = _mm_load_si128((__m128i *)&l1[k]);
			_mm_store_si128((__m128i *)&l1[k], _mm_add_epi64(chunk1, bx10)); k ^= 0x10;
			const __m128i chunk3 = _mm_load_si128((__m128i *)&l1[k]);
			_mm_store_si128((__m128i *)&l1[k], _mm_add_epi64(chunk2, ax1)); k ^= 0x20;
			_mm_store_si128((__m128i *)&l1[k], _mm_add_epi64(chunk3, bx11));
		}

		axl1 += hi;
		axh1 += lo;
		((uint64_t*)&l1[idx11])[0] = axl1;
		((uint64_t*)&l1[idx11])[1] = axh1;
		axh1 ^= ch;
		axl1 ^= cl;
		idx10 = axl1;
		idx11 = idx10 & 0x1FFFF0;

		if (PREFETCH)
			_mm_prefetch((const char*)&l1[idx11], _MM_HINT_T0);

		if (SHUFFLE)
		{
			bx01 = bx00;
			bx11 = bx10;
		}
		bx00 = cx0;
		bx10 = cx1;
	}

	//t2 = __rdtsc();

	// Optim - 90% time boundary
	cn_implode_scratchpad<MEM, SOFT_AES, PREFETCH>((__m128i*)ctx0->long_state, (__m128i*)ctx0->hash_state);
	cn_implode_scratchpad<MEM, SOFT_AES, PREFETCH>((__m128i*)ctx1->long_state, (__m128i*)ctx1->hash_state);

	// Optim - 99% time boundary

	keccakf((uint64_t*)ctx0->hash_state, 24);
	extra_hashes[ctx0->hash_state[0] & 3](ctx0->hash_state, 200, (char*)output1);
	keccakf((uint64_t*)ctx1->hash_state, 24);
	extra_hashes[ctx1->hash_state[0] & 3](ctx1->hash_state, 200, (char*)output2);
}
