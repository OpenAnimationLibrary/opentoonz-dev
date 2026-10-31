#include <cstdio>
#include <cstring>
#include <string>

#include "image/svg/tiio_svg.cpp"

namespace {

int fail(const char* message) {
  std::fprintf(stderr, "%s\n", message);
  return 1;
}

template <size_t Size>
bool matches(const char (&actual)[Size], const std::string& expected) {
  return std::memchr(actual, '\0', Size) && expected == actual;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return fail("expected one SVG test case");
  const std::string test = argv[1];
  std::string id = "normal", width = "10px", height = "20mm";
  std::string color = "rgb(12,34,56)";
  if (test == "id")
    id = std::string(4096, 'i');
  else if (test == "width")
    width = "10" + std::string(512, 'u');
  else if (test == "height")
    height = "20" + std::string(512, 'u');
  else if (test == "rgb-first")
    color = "rgb(1" + std::string(512, ' ') + "2,3)";
  else if (test == "rgb-second")
    color = "rgb(1,2" + std::string(512, ' ') + "3)";
  else if (test != "normal")
    return fail("unknown SVG test case");

  std::string input = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" +
                      width + "\" height=\"" + height + "\"><path id=\"" + id +
                      "\" fill=\"" + color + "\" d=\"M 0 0 L 1 1\"/></svg>";
  NSVGimage* image = nsvgParse(&input[0]);
  if (!image) return fail("expected a parsed SVG image");
  if (!image->shapes || !image->shapes->paths) {
    nsvgDelete(image);
    return fail("expected a parsed shape and path");
  }

  const bool valid =
      matches(image->shapes->id, id.substr(0, 49)) &&
      matches(image->wunits, test == "width" ? std::string(7, 'u') : "px") &&
      matches(image->hunits, test == "height" ? std::string(7, 'u') : "mm") &&
      image->width == 10 && image->height == 20 &&
      image->shapes->paths->npts == 4 && image->shapes->paths->svgPts[6] == 1 &&
      image->shapes->paths->svgPts[7] == 1 &&
      (test != "normal" ||
       image->shapes->fillColor == (NSVG_RGB(12, 34, 56) | 0xff000000u));
  nsvgDelete(image);
  return valid ? 0 : fail("SVG values or bounded strings did not match");
}
