// See images.h.
#include "render/images.h"

#include "render/draw.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#endif

namespace render
{
    ImageAtlas imageAtlas;

    namespace
    {
        constexpr int kAtlasSize = 2048, kPad = 2;

        struct Pixels
        {
            int w = 0, h = 0;
            std::vector<uint8_t> rgba; // premultiplied
        };

#ifdef _WIN32
        // WIC: decode, scale (Fant: averages, sharp enough at a quarter), convert to premultiplied RGBA
        bool decode(const std::filesystem::path &file, int maxSide, Pixels &out)
        {
            const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            IWICImagingFactory *factory = nullptr;
            IWICBitmapDecoder *decoder = nullptr;
            IWICBitmapFrameDecode *frame = nullptr;
            IWICBitmapScaler *scaler = nullptr;
            IWICFormatConverter *converter = nullptr;
            bool ok = false;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
                SUCCEEDED(factory->CreateDecoderFromFilename(file.wstring().c_str(), nullptr, GENERIC_READ,
                                                             WICDecodeMetadataCacheOnDemand, &decoder)) &&
                SUCCEEDED(decoder->GetFrame(0, &frame)))
            {
                UINT w = 0, h = 0;
                frame->GetSize(&w, &h);
                if (w && h)
                {
                    const double scale = std::min(1.0, double(maxSide) / double(std::max(w, h)));
                    const UINT sw = std::max<UINT>(1, UINT(w * scale + 0.5)), sh = std::max<UINT>(1, UINT(h * scale + 0.5));
                    IWICBitmapSource *source = frame;
                    if ((sw != w || sh != h) && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                        SUCCEEDED(scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant)))
                        source = scaler;
                    UINT ow = 0, oh = 0;
                    source->GetSize(&ow, &oh);
                    if (SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                        SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                                        WICBitmapPaletteTypeCustom)))
                    {
                        out.w = int(ow);
                        out.h = int(oh);
                        out.rgba.resize(size_t(ow) * oh * 4);
                        ok = SUCCEEDED(converter->CopyPixels(nullptr, ow * 4, UINT(out.rgba.size()), out.rgba.data()));
                    }
                }
            }
            for (IUnknown *p : std::initializer_list<IUnknown *>{converter, scaler, frame, decoder, factory})
                if (p)
                    p->Release();
            if (SUCCEEDED(init))
                CoUninitialize();
            return ok;
        }
#else
        // Linux / macOS: no decoder yet (docs/LAUNCHER.md: Home draws its own cover)
        bool decode(const std::filesystem::path &, int, Pixels &) { return false; }
#endif

        // a place in the atlas (shelves), or false when it is full
        bool place(int w, int h, int &x, int &y)
        {
            ImageAtlas &a = imageAtlas;
            if (a.size == 0)
            {
                a.size = kAtlasSize;
                a.rgba.assign(size_t(kAtlasSize) * kAtlasSize * 4, 0);
            }
            const int pw = w + 2 * kPad, ph = h + 2 * kPad;
            if (a.shelfX + pw > a.size)
            {
                a.shelfX = 0;
                a.shelfY += a.shelfH;
                a.shelfH = 0;
            }
            if (pw > a.size || a.shelfY + ph > a.size)
                return false;
            x = a.shelfX + kPad;
            y = a.shelfY + kPad;
            a.shelfX += pw;
            a.shelfH = std::max(a.shelfH, ph);
            return true;
        }
    }

    Image loadImage(const std::filesystem::path &file, int maxSide)
    {
        Image img;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec))
            return img;
        Pixels p;
        if (!decode(file, std::min(maxSide, kAtlasSize - 2 * kPad), p) || p.w <= 0 || p.h <= 0)
            return img;
        int x = 0, y = 0;
        if (!place(p.w, p.h, x, y))
            return img;
        ImageAtlas &a = imageAtlas;
        // the picture, and its edge pixels repeated into the padding (no dark seams when sampled at its border)
        for (int row = -kPad; row < p.h + kPad; ++row)
        {
            const int sy = std::clamp(row, 0, p.h - 1);
            for (int col = -kPad; col < p.w + kPad; ++col)
            {
                const int sx = std::clamp(col, 0, p.w - 1);
                std::memcpy(&a.rgba[(size_t(y + row) * a.size + size_t(x + col)) * 4], &p.rgba[(size_t(sy) * p.w + sx) * 4], 4);
            }
        }
        ++a.generation;
        img.ok = true;
        img.width = p.w;
        img.height = p.h;
        img.uv[0] = float(x) / float(a.size);
        img.uv[1] = float(y) / float(a.size);
        img.uv[2] = float(x + p.w) / float(a.size);
        img.uv[3] = float(y + p.h) / float(a.size);
        return img;
    }

    void drawImageCover(const Image &img, core::Rect dest, const float *mask, float focusX, float focusY, float zoom, float opacity)
    {
        if (!img.ok || dest.w <= 0 || dest.h <= 0)
            return;
        // the part of the picture shown, in its own fractions: as much as fits dest's shape, less when zoomed
        const float ia = float(img.width) / float(img.height), da = dest.w / dest.h;
        float fw = 1, fh = 1;
        if (ia > da)
            fw = da / ia;
        else
            fh = ia / da;
        fw /= std::max(zoom, 1.0f);
        fh /= std::max(zoom, 1.0f);
        const float cx = std::clamp(focusX, fw * 0.5f, 1 - fw * 0.5f), cy = std::clamp(focusY, fh * 0.5f, 1 - fh * 0.5f);
        const float du = img.uv[2] - img.uv[0], dv = img.uv[3] - img.uv[1];
        const float uv[4] = {img.uv[0] + du * (cx - fw * 0.5f), img.uv[1] + dv * (cy - fh * 0.5f), img.uv[0] + du * (cx + fw * 0.5f),
                             img.uv[1] + dv * (cy + fh * 0.5f)};
        image(dest, uv, mask, opacity);
    }

    void drawImageFit(const Image &img, core::Rect box, float opacity)
    {
        if (!img.ok || box.w <= 0 || box.h <= 0)
            return;
        const float s = std::min(box.w / float(img.width), box.h / float(img.height));
        image({box.x, box.y, float(img.width) * s, float(img.height) * s}, img.uv, nullptr, opacity);
    }
}
