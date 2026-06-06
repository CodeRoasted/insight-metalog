// insight.metalog.internal — lone import-std manifest (1.5.1 unwrap, §11.9) + global C fixed-width type
// re-exports (metalog source uses unqualified uint64_t/int64_t/size_t etc.).
export module insight.metalog.internal;
export import std;
export
{
    using std::int64_t;
    using std::uint16_t;
    using std::uint32_t;
    using std::uint64_t;
    using std::uint8_t;
    using std::size_t;
    using std::ptrdiff_t;
}
