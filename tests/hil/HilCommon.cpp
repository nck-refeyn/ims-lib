// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#include "HilTarget.h"
#include "ImageOps.h"
#include "TestSupport.h"

#include <cstdlib>
#include <sstream>

namespace hil {

    bool WritesAllowed()
    {
        const char* v = std::getenv("IMS_HIL_ALLOW_WRITE");
        return v && std::string(v) == "1";
    }

    std::string Describe(const iMS::IMSSystem& ims)
    {
        std::ostringstream ss;
        auto conn = ims.Connection();
        ss << (conn ? conn->Ident() : std::string("?")) << " " << ims.ConnPort() << ": ";
        if (ims.Ctlr().IsValid()) {
            const auto& v = ims.Ctlr().GetVersion();
            ss << ims.Ctlr().Model() << " v" << v.major << "." << v.minor << "." << v.revision;
        } else {
            ss << "no controller";
        }
        ss << " / ";
        if (ims.Synth().IsValid()) {
            const auto& v = ims.Synth().GetVersion();
            ss << ims.Synth().Model() << " v" << v.major << "." << v.minor << "." << v.revision;
        } else {
            ss << "no synthesiser";
        }
        return ss.str();
    }

    iMS::HostReport RegRead(iMS::HostReport::Actions a, std::uint16_t addr, std::uint16_t len)
    {
        iMS::HostReport r(a, iMS::HostReport::Dir::READ, addr);
        iMS::ReportFields f = r.Fields();
        f.len = len;
        r.Fields(f);
        return r;
    }

    iMS::Image RampImage(const iMS::IMSSystem& ims, std::size_t points)
    {
        const double lower = ims.Synth().GetCap().lowerFrequency;
        const double upper = ims.Synth().GetCap().upperFrequency;
        iMS::Image img("hil-ramp");
        for (std::size_t i = 0; i < points; i++) {
            const double f = lower + (upper - lower) * static_cast<double>(i) / static_cast<double>(points);
            img.AddPoint(iMS::ImagePoint(iMS::FAP(f, static_cast<double>(i % 101), static_cast<double>((i * 7) % 360))));
        }
        return img;
    }

    DownloadResult DownloadImage(const std::shared_ptr<iMS::IMSSystem>& ims, const iMS::Image& img, bool verify,
        std::chrono::seconds timeout)
    {
        using E = iMS::ImageDownloadEvents;
        const int events[] = { E::DOWNLOAD_FINISHED, E::DOWNLOAD_ERROR, E::DOWNLOAD_FAIL_MEMORY_FULL,
            E::DOWNLOAD_FAIL_TRANSFER_ABORT, E::VERIFY_SUCCESS, E::VERIFY_FAIL };

        DownloadResult r;
        ims_test::EventRecorder rec;
        iMS::ImageDownload dl(ims, img);
        for (int e : events) dl.ImageDownloadEventSubscribe(e, &rec);

        const auto t0 = std::chrono::steady_clock::now();
        if (!dl.StartDownload()) {
            r.error = "StartDownload refused";
        } else {
            const int e = rec.WaitForAny({ E::DOWNLOAD_FINISHED, E::DOWNLOAD_ERROR, E::DOWNLOAD_FAIL_MEMORY_FULL, E::DOWNLOAD_FAIL_TRANSFER_ABORT }, timeout);
            r.elapsed = std::chrono::steady_clock::now() - t0;
            if (e == E::DOWNLOAD_FINISHED) {
                r.finished = true;
                r.bytes = rec.Events(E::DOWNLOAD_FINISHED)[0].param;
            } else {
                r.error = e < 0 ? "download timed out" : "download failed (event " + std::to_string(e) + ")";
            }
        }

        if (verify && r.finished) {
            if (!dl.StartVerify()) {
                r.error = "StartVerify refused";
            } else {
                const int e = rec.WaitForAny({ E::VERIFY_SUCCESS, E::VERIFY_FAIL }, timeout);
                r.verified = e == E::VERIFY_SUCCESS;
                if (e == E::VERIFY_FAIL) r.error = "verify failed, first error at " + std::to_string(rec.Events(E::VERIFY_FAIL)[0].param);
                else if (e < 0) r.error = "verify timed out";
            }
        }

        for (int e : events) dl.ImageDownloadEventUnsubscribe(e, &rec);
        return r;
    }

    void EraseImage(const std::shared_ptr<iMS::IMSSystem>& ims, const iMS::Image& img)
    {
        iMS::ImageTableViewer itv(ims);
        for (auto it = itv.begin(); it != itv.end(); ++it) {
            if (it->Matches(img)) {
                itv.Erase(*it);
                return;
            }
        }
    }

}
