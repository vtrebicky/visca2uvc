#include <libuvc/libuvc.h>

#include <iostream>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"

namespace {

#define RETURN_IF_UVC_ERROR(expr)                                             \
  if (const uvc_error err = (expr); err < 0) {                                \
    return absl::InternalError(absl::StrCat(#expr, ": ", uvc_strerror(err))); \
  }

struct ZoomRel {
  int8_t zoom_rel;
  uint8_t digital_zoom;
  uint8_t speed;

  friend std::ostream& operator<<(std::ostream& os, const ZoomRel& value) {
    os << "zoom_rel: " << value.zoom_rel
       << ", digital_zoom: " << value.digital_zoom
       << ", speed: " << value.speed;
    return os;
  }
};

class UvcDelete {
 public:
  void operator()(uvc_device_handle_t* ptr) noexcept { uvc_close(ptr); }
  void operator()(uvc_device_t* ptr) noexcept { uvc_unref_device(ptr); }
  void operator()(uvc_context_t* ptr) noexcept { uvc_exit(ptr); }
};

template <typename T>
using UvcUniquePtr = std::unique_ptr<T, UvcDelete>;

class UvcDeviceHandle {
 public:
  using Ptr = UvcUniquePtr<uvc_device_handle_t>;
  explicit UvcDeviceHandle(Ptr handle) : handle_(std::move(handle)) {}

  absl::StatusOr<uint16_t> GetZoomAbs(uvc_req_code req_code) const {
    uint16_t result;
    RETURN_IF_UVC_ERROR(uvc_get_zoom_abs(handle_.get(), &result, req_code));
    return result;
  }

  absl::Status SetZoomAbs(uint16_t focal_length) {
    RETURN_IF_UVC_ERROR(uvc_set_zoom_abs(handle_.get(), focal_length));
    return absl::OkStatus();
  }

  absl::StatusOr<ZoomRel> GetZoomRel(uvc_req_code req_code) const {
    ZoomRel result;
    RETURN_IF_UVC_ERROR(uvc_get_zoom_rel(handle_.get(), &result.zoom_rel,
                                         &result.digital_zoom, &result.speed,
                                         req_code));
    return result;
  }

  absl::Status SetZoomRel(const ZoomRel& zoom) {
    RETURN_IF_UVC_ERROR(uvc_set_zoom_rel(handle_.get(), zoom.zoom_rel,
                                         zoom.digital_zoom, zoom.speed));
    return absl::OkStatus();
  }

  void PrintDiag(FILE* file) const { uvc_print_diag(handle_.get(), file); }

 private:
  Ptr handle_;
};

class UvcDevice {
 public:
  using Ptr = UvcUniquePtr<uvc_device_t>;
  explicit UvcDevice(Ptr dev) : dev_(std::move(dev)) {}

  absl::StatusOr<UvcDeviceHandle> Open() {
    uvc_device_handle_t* handle;
    RETURN_IF_UVC_ERROR(uvc_open(dev_.get(), &handle));
    return UvcDeviceHandle(UvcDeviceHandle::Ptr(handle));
  }

 private:
  Ptr dev_;
};

class UvcContext {
 public:
  using Ptr = UvcUniquePtr<uvc_context_t>;
  explicit UvcContext(Ptr ctx) : ctx_(std::move(ctx)) {}

  static absl::StatusOr<UvcContext> Create() {
    uvc_context_t* ctx;
    RETURN_IF_UVC_ERROR(uvc_init(&ctx, /*usb_ctx=*/nullptr));
    return UvcContext(Ptr(ctx));
  }

  absl::StatusOr<UvcDevice> FindDevice(int vid, int pid, const char* sn) {
    uvc_device_t* dev;
    RETURN_IF_UVC_ERROR(uvc_find_device(ctx_.get(), &dev, vid, pid, sn));
    return UvcDevice(UvcDevice::Ptr(dev));
  }

 private:
  Ptr ctx_;
};

template <typename T>
absl::StatusOr<T> SimpleAtoi(absl::string_view str) {
  uint32_t result;
  if (!absl::SimpleAtoi(str, &result)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot parse as ", typeid(T).name(), ": ", str));
  }
  return T(result);
}

absl::Status Visca2Uvc(const absl::Span<char* const> args) {
  if (args.size() <= 1) {
    std::cout << R"(Usage: visca2uvc [cmd] ...

  get_zoom_abs
  set_zoom_abs focal_length

  get_zoom_rel
  set_zoom_rel zoom_rel digital_zoom speed
)";
    return absl::OkStatus();
  }

  auto uvc = UvcContext::Create().value();
  // Get the first available UVC device.
  UvcDevice dev = uvc.FindDevice(/*vid=*/0, /*pid=*/0, /*sn=*/nullptr).value();
  UvcDeviceHandle handle = dev.Open().value();
  handle.PrintDiag(stdout);

  const absl::string_view cmd = args[1];
  if (cmd == "get_zoom_abs") {
    std::cout << "min: " << handle.GetZoomAbs(UVC_GET_MIN).value() << "\n";
    std::cout << "max: " << handle.GetZoomAbs(UVC_GET_MAX).value() << "\n";
    std::cout << "cur: " << handle.GetZoomAbs(UVC_GET_CUR).value() << "\n";
  } else if (cmd == "set_zoom_abs") {
    if (args.size() != 3) {
      return absl::InvalidArgumentError("set_zoom_abs needs 1 argument.");
    }
    std::cout << "set: "
              << handle.SetZoomAbs(SimpleAtoi<uint16_t>(args[2]).value())
              << "\n";
    std::cout << "cur: " << handle.GetZoomAbs(UVC_GET_CUR).value() << "\n";
  } else if (cmd == "get_zoom_rel") {
    std::cout << "min: " << handle.GetZoomRel(UVC_GET_MIN).value() << "\n";
    std::cout << "max: " << handle.GetZoomRel(UVC_GET_MAX).value() << "\n";
    std::cout << "cur: " << handle.GetZoomRel(UVC_GET_CUR).value() << "\n";
  } else if (cmd == "set_zoom_rel") {
    if (args.size() != 5) {
      return absl::InvalidArgumentError("set_zoom_rel needs 3 argument.");
    }
    ZoomRel arg;
    arg.zoom_rel = SimpleAtoi<int8_t>(args[2]).value();
    arg.digital_zoom = SimpleAtoi<uint8_t>(args[3]).value();
    arg.speed = SimpleAtoi<uint8_t>(args[3]).value();
    std::cout << "set: " << handle.SetZoomRel(arg) << "\n";
    std::cout << "cur: " << handle.GetZoomRel(UVC_GET_CUR).value() << "\n";
  } else {
    std::cerr << "Unknown command: " << cmd << "\n";
  }

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const absl::Status status =
        Visca2Uvc(absl::MakeConstSpan(absl::ParseCommandLine(argc, argv)));
    if (!status.ok()) {
      std::cerr << status << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }
}
