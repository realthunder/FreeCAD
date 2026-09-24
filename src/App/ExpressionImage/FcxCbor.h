/* Decoding the wire's CBOR, in a form every standard library accepts.
 *
 * nlohmann's binary reader takes its character type from the iterator it
 * is given and then instantiates std::char_traits for it, and the
 * standard defines char_traits only for the character types it names.
 * libstdc++ ships std::char_traits<unsigned char> as an extension, so a
 * range over a std::vector<unsigned char> -- which is what every buffer
 * on this wire is -- compiles on Linux and fails on libc++ with
 * "implicit instantiation of undefined template".  Bytes cross as char
 * here, which is defined everywhere.
 */
#ifndef APP_FCX_CBOR_H
#define APP_FCX_CBOR_H

#include <cstddef>

#include <nlohmann/json.hpp>

namespace FcxWire
{

/// One CBOR value out of a byte buffer.
inline nlohmann::json fromCbor(const void* data, std::size_t len)
{
    const char* p = static_cast<const char*>(data);
    return nlohmann::json::from_cbor(p, p + len);
}

/// The same over any contiguous byte container (std::vector<unsigned
/// char>, std::string, ...).
template<class Bytes>
inline nlohmann::json fromCbor(const Bytes& bytes)
{
    return fromCbor(bytes.data(), bytes.size());
}

}  // namespace FcxWire

#endif  // APP_FCX_CBOR_H
