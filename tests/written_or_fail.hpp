#pragma once

// refs: DN-99.D8
// pre: included by a gtest TU after its module import, so gtest and std are visible.
// post: the written document, or an empty string after a recorded failure naming the path of the
// NaN or infinity that refused it.
[[nodiscard]] inline std::string
written_or_fail(const std::expected<std::string, std::string>& written)
{
    if (!written)
    {
        ADD_FAILURE() << "the document was refused: a NaN or an infinity at '" << written.error()
                      << "'";
        return {};
    }
    return *written;
}
