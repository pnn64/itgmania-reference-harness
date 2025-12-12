#ifndef PICOSHA2_H
#define PICOSHA2_H
#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
namespace picosha2 {
using byte_t = unsigned char;
using word_t = unsigned int;
static const word_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline word_t ch(word_t x, word_t y, word_t z) { return (x & y) ^ ((~x) & z); }
inline word_t maj(word_t x, word_t y, word_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline word_t rotr(word_t x, word_t n) { return (x >> n) | (x << (32 - n)); }
inline word_t bsig0(word_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline word_t bsig1(word_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline word_t ssig0(word_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline word_t ssig1(word_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

inline void init_hash(std::vector<word_t>& h) {
    h.resize(8);
    h[0] = 0x6a09e667;
    h[1] = 0xbb67ae85;
    h[2] = 0x3c6ef372;
    h[3] = 0xa54ff53a;
    h[4] = 0x510e527f;
    h[5] = 0x9b05688c;
    h[6] = 0x1f83d9ab;
    h[7] = 0x5be0cd19;
}

inline void transform(const byte_t* chunk, std::vector<word_t>& h) {
    word_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (chunk[i * 4] << 24) | (chunk[i * 4 + 1] << 16) | (chunk[i * 4 + 2] << 8) | (chunk[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = ssig1(w[i - 2]) + w[i - 7] + ssig0(w[i - 15]) + w[i - 16];
    }

    word_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        word_t t1 = hh + bsig1(e) + ch(e, f, g) + k[i] + w[i];
        word_t t2 = bsig0(a) + maj(a, b, c);
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

inline void hash256(const std::string& src, std::vector<byte_t>& digest) {
    std::vector<word_t> h;
    init_hash(h);

    std::vector<byte_t> data(src.begin(), src.end());
    size_t bit_len = data.size() * 8;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) {
        data.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<byte_t>((bit_len >> (i * 8)) & 0xff));
    }

    for (size_t i = 0; i < data.size(); i += 64) {
        transform(&data[i], h);
    }

    digest.resize(32);
    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = (h[i] >> 24) & 0xff;
        digest[i * 4 + 1] = (h[i] >> 16) & 0xff;
        digest[i * 4 + 2] = (h[i] >> 8) & 0xff;
        digest[i * 4 + 3] = h[i] & 0xff;
    }
}

inline std::string bytes_to_hex_string(const std::vector<byte_t>& bytes) {
    std::ostringstream oss;
    for (auto b : bytes) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

inline std::string hash256_hex_string(const std::string& src) {
    std::vector<byte_t> digest;
    hash256(src, digest);
    return bytes_to_hex_string(digest);
}

}  // namespace picosha2
#endif
