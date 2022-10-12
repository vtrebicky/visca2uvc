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

template <typename T, void (*deleter)(T*)>
class UvcWrapper {
 protected:
  explicit UvcWrapper(T* value) : value_(value) {}
  ~UvcWrapper() {
    if (value_ != nullptr) deleter(value_);
  }
  UvcWrapper(const UvcWrapper&) = delete;
  UvcWrapper& operator=(const UvcWrapper&) = delete;
  UvcWrapper(UvcWrapper&& other) : value_(other.value_) {
    other.value_ = nullptr;
  }
  UvcWrapper& operator=(UvcWrapper&& other) {
    std::swap(other.value_, value_);
    return *this;
  }

  T* value() const { return value_; }

 private:
  T* value_;
};

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

class UvcDeviceHandle : public UvcWrapper<uvc_device_handle_t, &uvc_close> {
 public:
  absl::StatusOr<uint16_t> GetZoomAbs(uvc_req_code req_code) const {
    uint16_t result;
    if (const uvc_error_t err = uvc_get_zoom_abs(value(), &result, req_code);
        err < 0) {
      return absl::InternalError(
          absl::StrCat("uvc_get_zoom_abs: ", uvc_strerror(err)));
    }
    return result;
  }

  absl::Status SetZoomAbs(uint16_t focal_length) {
    if (const uvc_error_t err = uvc_set_zoom_abs(value(), focal_length);
        err < 0) {
      return absl::InternalError(
          absl::StrCat("uvc_set_zoom_abs: ", uvc_strerror(err)));
    }
    return absl::OkStatus();
  }

  absl::StatusOr<ZoomRel> GetZoomRel(uvc_req_code req_code) const {
    ZoomRel result;
    if (const uvc_error_t err =
            uvc_get_zoom_rel(value(), &result.zoom_rel, &result.digital_zoom,
                             &result.speed, req_code);
        err < 0) {
      return absl::InternalError(
          absl::StrCat("uvc_get_zoom_rel: ", uvc_strerror(err)));
    }
    return result;
  }

  absl::Status SetZoomRel(const ZoomRel& zoom) {
    if (const uvc_error_t err = uvc_set_zoom_rel(value(), zoom.zoom_rel,
                                                 zoom.digital_zoom, zoom.speed);
        err < 0) {
      return absl::InternalError(
          absl::StrCat("uvc_set_zoom_rel: ", uvc_strerror(err)));
    }
    return absl::OkStatus();
  }

  void PrintDiag(FILE* file) const { uvc_print_diag(value(), file); }

 private:
  explicit UvcDeviceHandle(uvc_device_handle_t* handle) : UvcWrapper(handle) {}
  friend class UvcDevice;
};

class UvcDevice : public UvcWrapper<uvc_device_t, &uvc_unref_device> {
 public:
  absl::StatusOr<UvcDeviceHandle> Open() {
    uvc_device_handle_t* handle;
    if (const uvc_error_t err = uvc_open(value(), &handle); err < 0) {
      return absl::InternalError(absl::StrCat("uvc_open: ", uvc_strerror(err)));
    }
    return UvcDeviceHandle(handle);
  }

 private:
  explicit UvcDevice(uvc_device_t* dev) : UvcWrapper(dev) {}
  friend class UvcContext;
};

class UvcContext : public UvcWrapper<uvc_context_t, &uvc_exit> {
 public:
  static absl::StatusOr<UvcContext> Create() {
    uvc_context_t* ctx;
    if (const uvc_error_t err = uvc_init(&ctx, /*usb_ctx=*/nullptr); err < 0) {
      return absl::InternalError(absl::StrCat("uvc_init: ", uvc_strerror(err)));
    }
    return UvcContext(ctx);
  }

  absl::StatusOr<UvcDevice> FindDevice(int vid, int pid, const char* sn) {
    uvc_device_t* dev;
    if (const uvc_error_t err = uvc_find_device(value(), &dev, vid, pid, sn);
        err < 0) {
      return absl::InternalError(
          absl::StrCat("uvc_find_device: ", uvc_strerror(err)));
    }
    return UvcDevice(dev);
  }

 private:
  explicit UvcContext(uvc_context_t* ctx) : UvcWrapper(ctx) {}
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
