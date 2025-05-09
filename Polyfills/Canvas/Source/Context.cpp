#include <bx/math.h>
#include <map>
#include <algorithm>
#include <cassert>
#include <regex>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "nanovg/nanovg.h"
#include "nanovg_babylon.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION

#include "Canvas.h"
#include "Context.h"
#include "MeasureText.h"
#include "Image.h"
#include "ImageData.h"
#include "Colors.h"

/*
Most of these context methods are preliminary work. They are currenbly not tested properly.
*/
namespace Babylon::Polyfills::Internal
{
    static constexpr auto JS_CONTEXT_CONSTRUCTOR_NAME = "Context";

    void Context::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};

        Napi::Function func = DefineClass(
            env,
            JS_CONTEXT_CONSTRUCTOR_NAME,
            {
                InstanceMethod("clearRect", &Context::ClearRect),
                InstanceMethod("save", &Context::Save),
                InstanceMethod("restore", &Context::Restore),
                InstanceMethod("fillRect", &Context::FillRect),
                InstanceMethod("scale", &Context::Scale),
                InstanceMethod("rotate", &Context::Rotate),
                InstanceMethod("translate", &Context::Translate),
                InstanceMethod("strokeRect", &Context::StrokeRect),
                InstanceMethod("rect", &Context::Rect),
                InstanceMethod("clip", &Context::Clip),
                InstanceMethod("putImageData", &Context::PutImageData),
                InstanceMethod("arc", &Context::Arc),
                InstanceMethod("beginPath", &Context::BeginPath),
                InstanceMethod("closePath", &Context::ClosePath),
                InstanceMethod("moveTo", &Context::MoveTo),
                InstanceMethod("lineTo", &Context::LineTo),
                InstanceMethod("quadraticCurveTo", &Context::QuadraticCurveTo),
                InstanceMethod("measureText", &Context::MeasureText),
                InstanceMethod("stroke", &Context::Stroke),
                InstanceMethod("fill", &Context::Fill),
                InstanceMethod("drawImage", &Context::DrawImage),
                InstanceMethod("getImageData", &Context::GetImageData),
                InstanceMethod("setLineDash", &Context::SetLineDash),
                InstanceMethod("fillText", &Context::FillText),
                InstanceMethod("strokeText", &Context::StrokeText),
                InstanceMethod("createLinearGradient", &Context::CreateLinearGradient),
                InstanceMethod("setTransform", &Context::SetTransform),
                InstanceMethod("dispose", &Context::Dispose),
                InstanceMethod("flush", &Context::Flush),
                InstanceAccessor("lineJoin", &Context::GetLineJoin, &Context::SetLineJoin),
                InstanceAccessor("miterLimit", &Context::GetMiterLimit, &Context::SetMiterLimit),
                InstanceAccessor("font", &Context::GetFont, &Context::SetFont),
                InstanceAccessor("strokeStyle", &Context::GetStrokeStyle, &Context::SetStrokeStyle),
                InstanceAccessor("fillStyle", &Context::GetFillStyle, &Context::SetFillStyle),
                InstanceAccessor("globalAlpha", nullptr, &Context::SetGlobalAlpha),
                InstanceAccessor("shadowColor", &Context::GetShadowColor, &Context::SetShadowColor),
                InstanceAccessor("shadowBlur", &Context::GetShadowBlur, &Context::SetShadowBlur),
                InstanceAccessor("shadowOffsetX", &Context::GetShadowOffsetX, &Context::SetShadowOffsetX),
                InstanceAccessor("shadowOffsetY", &Context::GetShadowOffsetY, &Context::SetShadowOffsetY),
                InstanceAccessor("lineWidth", &Context::GetLineWidth, &Context::SetLineWidth),
            });
        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_CONTEXT_CONSTRUCTOR_NAME, func);
    }

    Napi::Value Context::CreateInstance(Napi::Env env, Napi::Value canvas)
    {
        auto func = JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_CONTEXT_CONSTRUCTOR_NAME).As<Napi::Function>();
        return func.New({canvas});
    }

    Context::Context(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<Context>{info}
        , m_canvas{NativeCanvas::Unwrap(info[0].As<Napi::Object>())}
        , m_nvg{nvgCreate(1)}
        , m_graphicsContext{m_canvas->GetGraphicsContext()}
        , m_update{m_graphicsContext.GetUpdate("update")}
        , m_cancellationSource{std::make_shared<arcana::cancellation_source>()}
        , m_runtimeScheduler{Babylon::JsRuntime::GetFromJavaScript(info.Env())}
        , Polyfills::Canvas::Impl::MonitoredResource{Polyfills::Canvas::Impl::GetFromJavaScript(info.Env())}
    {
        // TODO: commented code doesn't compile with napi-jsi. Using non read-only property for now
        //info.This().ToObject().DefineProperty(Napi::PropertyDescriptor::Value("canvas", info[0], napi_enumerable));
        info.This().ToObject().Set("canvas", info[0]);

        for (auto& font : NativeCanvas::fontsInfos)
        {
            m_fonts[font.first] = nvgCreateFontMem(m_nvg, font.first.c_str(), font.second.data(), static_cast<int>(font.second.size()), 0);
        }
    }

    Context::~Context()
    {
        Dispose();
        m_cancellationSource->cancel();
    }

    void Context::Dispose(const Napi::CallbackInfo&)
    {
        Dispose();
    }

    void Context::FlushGraphicResources()
    {
        Dispose();
    }

    void Context::Dispose()
    {
        if (m_nvg)
        {
            for (auto& image : m_nvgImageIndices)
            {
                nvgDeleteImage(m_nvg, image.second);
            }
            nvgDelete(m_nvg);
            m_nvg = nullptr;
        }

        m_isClipped = false;
    }

    void Context::FillRect(const Napi::CallbackInfo& info)
    {
        auto left = info[0].As<Napi::Number>().FloatValue();
        auto top = info[1].As<Napi::Number>().FloatValue();
        auto width = info[2].As<Napi::Number>().FloatValue();
        auto height = info[3].As<Napi::Number>().FloatValue();

        if (!m_isClipped)
        {
            nvgBeginPath(m_nvg);
        }

        nvgRect(m_nvg, left, top, width, height);

        const auto color = StringToColor(info.Env(), m_fillStyle);
        nvgFillColor(m_nvg, color);
        nvgFill(m_nvg);
    }

    Napi::Value Context::GetFillStyle(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_fillStyle);
    }

    void Context::SetFillStyle(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        m_fillStyle = value.As<Napi::String>().Utf8Value();
        const auto color = StringToColor(info.Env(), m_fillStyle);
        nvgFillColor(m_nvg, color);
    }

    Napi::Value Context::GetStrokeStyle(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_strokeStyle);
    }

    void Context::SetStrokeStyle(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        m_strokeStyle = value.As<Napi::String>().Utf8Value();
        auto color = StringToColor(info.Env(), m_strokeStyle);
        nvgStrokeColor(m_nvg, color);
    }

    Napi::Value Context::GetLineWidth(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_lineWidth);
    }

    void Context::SetLineWidth(const Napi::CallbackInfo&, const Napi::Value& value)
    {
        m_lineWidth = value.As<Napi::Number>().FloatValue();
        nvgStrokeWidth(m_nvg, m_lineWidth);
    }

    void Context::Fill(const Napi::CallbackInfo&)
    {
        nvgFill(m_nvg);
    }

    void Context::Save(const Napi::CallbackInfo&)
    {
        nvgSave(m_nvg);
    }

    void Context::Restore(const Napi::CallbackInfo&)
    {
        nvgRestore(m_nvg);
        m_isClipped = false;
    }

    void Context::ClearRect(const Napi::CallbackInfo& info)
    {
        const float x = info[0].As<Napi::Number>().FloatValue();
        const float y = info[1].As<Napi::Number>().FloatValue();
        const float width = info[2].As<Napi::Number>().FloatValue();
        const float height = info[3].As<Napi::Number>().FloatValue();

        nvgSave(m_nvg);
        nvgGlobalCompositeOperation(m_nvg, NVG_COPY);

        if (!m_isClipped)
        {
            nvgBeginPath(m_nvg);
        }

        nvgRect(m_nvg, x, y, width, height);

        if (!m_isClipped)
        {
            nvgClosePath(m_nvg);
        }

        nvgFillColor(m_nvg, TRANSPARENT_BLACK);
        nvgFill(m_nvg);
        nvgRestore(m_nvg);
    }

    void Context::Translate(const Napi::CallbackInfo& info)
    {
        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();
        nvgTranslate(m_nvg, x, y);
    }

    void Context::Rotate(const Napi::CallbackInfo& info)
    {
        const auto angle = info[0].As<Napi::Number>().FloatValue();
        nvgRotate(m_nvg, angle);
    }

    void Context::Scale(const Napi::CallbackInfo& info)
    {
        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();
        nvgScale(m_nvg, x, y);
    }

    void Context::BeginPath(const Napi::CallbackInfo&)
    {
        nvgBeginPath(m_nvg);
    }

    void Context::ClosePath(const Napi::CallbackInfo&)
    {
        nvgClosePath(m_nvg);
    }

    void Context::Rect(const Napi::CallbackInfo& info)
    {
        const auto left = info[0].As<Napi::Number>().FloatValue();
        const auto top = info[1].As<Napi::Number>().FloatValue();
        const auto width = info[2].As<Napi::Number>().FloatValue();
        const auto height = info[3].As<Napi::Number>().FloatValue();

        nvgRect(m_nvg, left, top, width, height);
        m_rectangleClipping = {left, top, width, height};
    }

    void Context::Clip(const Napi::CallbackInfo& /*info*/)
    {
        m_isClipped = true;

        //By default m_rectangleClipping is not set, in this case we use the canvas width and height.
        auto w = m_rectangleClipping.width != 0 ? m_rectangleClipping.width : m_canvas->GetWidth();
        auto h = m_rectangleClipping.height != 0 ? m_rectangleClipping.height : m_canvas->GetHeight();

        // expand clipping 1pix in each direction because nanovg AA gets cut a bit short.
        nvgScissor(m_nvg, m_rectangleClipping.left - 1, m_rectangleClipping.top - 1, w + 1, h + 1);
    }

    void Context::StrokeRect(const Napi::CallbackInfo& info)
    {
        const auto left = info[0].As<Napi::Number>().FloatValue();
        const auto top = info[1].As<Napi::Number>().FloatValue();
        const auto width = info[2].As<Napi::Number>().FloatValue();
        const auto height = info[3].As<Napi::Number>().FloatValue();

        nvgRect(m_nvg, left, top, width, height);
        nvgStroke(m_nvg);
    }

    void Context::Stroke(const Napi::CallbackInfo&)
    {
        nvgStroke(m_nvg);
    }

    void Context::MoveTo(const Napi::CallbackInfo& info)
    {
        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();

        nvgMoveTo(m_nvg, x, y);
    }

    void Context::LineTo(const Napi::CallbackInfo& info)
    {
        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();

        nvgLineTo(m_nvg, x, y);
    }

    void Context::QuadraticCurveTo(const Napi::CallbackInfo& info)
    {
        const auto cx = info[0].As<Napi::Number>().FloatValue();
        const auto cy = info[1].As<Napi::Number>().FloatValue();
        const auto x = info[2].As<Napi::Number>().FloatValue();
        const auto y = info[3].As<Napi::Number>().FloatValue();

        nvgBezierTo(m_nvg, cx, cy, cx, cy, x, y);
    }

    Napi::Value Context::MeasureText(const Napi::CallbackInfo& info)
    {
        std::string text{info[0].As<Napi::String>()};
        return MeasureText::CreateInstance(info.Env(), this, text);
    }

    void Context::FillText(const Napi::CallbackInfo& info)
    {
        const std::string text = info[0].As<Napi::String>().Utf8Value();
        auto x = info[1].As<Napi::Number>().FloatValue();
        auto y = info[2].As<Napi::Number>().FloatValue();

        if (!m_fonts.empty())
        {
            if (m_currentFontId >= 0)
            {
                nvgFontFaceId(m_nvg, m_currentFontId);
            }
            else
            {
                nvgFontFaceId(m_nvg, m_fonts.begin()->second);
            }

            nvgText(m_nvg, x, y, text.c_str(), nullptr);
        }
    }

    void Context::Flush(const Napi::CallbackInfo&)
    {
        m_canvas->UpdateRenderTarget();

        Graphics::FrameBuffer& frameBuffer = m_canvas->GetFrameBuffer();

        auto updateToken{m_update.GetUpdateToken()};
        bgfx::Encoder* encoder = updateToken.GetEncoder();
        frameBuffer.Bind(*encoder);
        frameBuffer.SetViewPort(*encoder, 0.f, 0.f, 1.f, 1.f);
        const auto width = m_canvas->GetWidth();
        const auto height = m_canvas->GetHeight();

        nvgBeginFrame(m_nvg, float(width), float(height), 1.0f);
        nvgSetFrameBufferAndEncoder(m_nvg, frameBuffer, encoder);
        nvgEndFrame(m_nvg);
        frameBuffer.Unbind(*encoder);
    }

    void Context::PutImageData(const Napi::CallbackInfo&)
    {
        throw std::runtime_error{"not implemented"};
    }

    void Context::Arc(const Napi::CallbackInfo& info)
    {
        const auto x = static_cast<float>(info[0].As<Napi::Number>().DoubleValue());
        const auto y = static_cast<float>(info[1].As<Napi::Number>().DoubleValue());
        const auto radius = static_cast<float>(info[2].As<Napi::Number>().DoubleValue());
        const auto startAngle = static_cast<float>(info[3].As<Napi::Number>().DoubleValue());
        const auto endAngle = static_cast<float>(info[4].As<Napi::Number>().DoubleValue());
        const NVGwinding winding = (info.Length() == 6 && info[5].As<Napi::Boolean>()) ? NVGwinding::NVG_CCW : NVGwinding::NVG_CW;
        nvgArc(m_nvg, x, y, radius, startAngle, endAngle, winding);
    }

    void Context::DrawImage(const Napi::CallbackInfo& info)
    {
        const NativeCanvasImage* canvasImage = NativeCanvasImage::Unwrap(info[0].As<Napi::Object>());

        int imageIndex{-1};
        const auto nvgImageIter = m_nvgImageIndices.find(canvasImage);
        if (nvgImageIter == m_nvgImageIndices.end())
        {
            imageIndex = canvasImage->CreateNVGImageForContext(m_nvg);
            m_nvgImageIndices.try_emplace(canvasImage, imageIndex);
        }
        else
        {
            imageIndex = nvgImageIter->second;
        }
        assert(imageIndex != -1);

        if (info.Length() == 3)
        {
            const auto dx = static_cast<float>(info[1].As<Napi::Number>().Int32Value());
            const auto dy = static_cast<float>(info[2].As<Napi::Number>().Int32Value());
            const auto width = static_cast<float>(canvasImage->GetWidth());
            const auto height = static_cast<float>(canvasImage->GetHeight());

            NVGpaint imagePaint = nvgImagePattern(m_nvg, 0.f, 0.f, width, height, 0.f, imageIndex, 1.f);

            if (!m_isClipped)
            {
                nvgBeginPath(m_nvg);
            }

            nvgRect(m_nvg, dx, dy, width, height);
            nvgFillPaint(m_nvg, imagePaint);
            nvgFill(m_nvg);
        }
        else if (info.Length() == 5)
        {
            const auto dx = static_cast<float>(info[1].As<Napi::Number>().Int32Value());
            const auto dy = static_cast<float>(info[2].As<Napi::Number>().Int32Value());
            const auto dWidth = static_cast<float>(info[3].As<Napi::Number>().Uint32Value());
            const auto dHeight = static_cast<float>(info[4].As<Napi::Number>().Uint32Value());

            NVGpaint imagePaint = nvgImagePattern(m_nvg, dx, dy, dWidth, dHeight, 0.f, imageIndex, 1.f);

            if (!m_isClipped)
            {
                nvgBeginPath(m_nvg);
            }

            nvgRect(m_nvg, dx, dy, dWidth, dHeight);
            nvgFillPaint(m_nvg, imagePaint);
            nvgFill(m_nvg);
        }
        else if (info.Length() == 9)
        {
            const auto sx = info[1].As<Napi::Number>().Int32Value();
            const auto sy = info[2].As<Napi::Number>().Int32Value();
            const auto sWidth = info[3].As<Napi::Number>().Uint32Value();
            const auto sHeight = info[4].As<Napi::Number>().Uint32Value();
            const auto dx = static_cast<float>(info[5].As<Napi::Number>().Int32Value());
            const auto dy = static_cast<float>(info[6].As<Napi::Number>().Int32Value());
            const auto dWidth = static_cast<float>(info[7].As<Napi::Number>().Uint32Value());
            const auto dHeight = static_cast<float>(info[8].As<Napi::Number>().Uint32Value());
            const auto width = static_cast<float>(canvasImage->GetWidth());
            const auto height = static_cast<float>(canvasImage->GetHeight());

            NVGpaint imagePaint = nvgImagePattern(m_nvg, dx, dy, dWidth, dHeight, 0.f, imageIndex, 1.f);

            if (!m_isClipped)
            {
                nvgBeginPath(m_nvg);
            }

            nvgRect(m_nvg, dx, dy, dWidth, dHeight);
            nvgFillPaint(m_nvg, imagePaint);
            nvgFill(m_nvg);
        }
        else
        {
            throw Napi::Error::New(info.Env(), "Invalid number of parameters for DrawImage");
        }
    }

    Napi::Value Context::GetImageData(const Napi::CallbackInfo& info)
    {
        // TODO: support source x and y
        //const auto sx = info[0].As<Napi::Number>().Uint32Value();
        //const auto sy = info[1].As<Napi::Number>().Uint32Value();
        const auto sw = info[2].As<Napi::Number>().Uint32Value();
        const auto sh = info[3].As<Napi::Number>().Uint32Value();

        return ImageData::CreateInstance(info.Env(), this, sw, sh);
    }

    void Context::SetLineDash(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    void Context::StrokeText(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    Napi::Value Context::CreateLinearGradient(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    void Context::SetTransform(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    Napi::Value Context::GetLineJoin(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    void Context::SetLineJoin(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    Napi::Value Context::GetMiterLimit(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    void Context::SetMiterLimit(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    Napi::Value Context::GetFont(const Napi::CallbackInfo& info)
    {
        return Napi::Value::From(Env(), m_font);
    }

    void Context::SetFont(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        if (!value.IsString())
        {
            throw Napi::Error::New(info.Env(), "invalid argument");
        }

        const std::string fontOptions = value.ToString();

        // Default font id, and font size values.
        // TODO: Determine better way of signaling to user that font specified is invalid.
        m_currentFontId = -1;
        float fontSize{16.f};

        // Regex to parse font styling information. For now we are only capturing font size (capture group 3) and font family name (capture group 4).
        static const std::regex fontStyleRegex("([[a-zA-Z]+\\s+)*((\\d+(\\.\\d+)?)px\\s+)?(\\w+)");
        std::smatch fontStyleMatch;

        // Perform the actual regex_match.
        if (std::regex_match(fontOptions, fontStyleMatch, fontStyleRegex))
        {
            // Check if font size was specified.
            if (fontStyleMatch[3].matched)
            {
                fontSize = std::stof(fontStyleMatch[3]);
            }

            // Check if the specified font family name is valid, and if so assign the current font id.
            if (m_fonts.find(fontStyleMatch[4]) != m_fonts.end())
            {
                m_currentFontId = m_fonts.at(fontStyleMatch[4]);
                m_font = fontOptions;
            }
        }

        // Set font size on the current context.
        nvgFontSize(m_nvg, fontSize);
    }

    void Context::SetGlobalAlpha(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        const float alpha = value.As<Napi::Number>().FloatValue();
        nvgGlobalAlpha(m_nvg, alpha);
    }

    Napi::Value Context::GetShadowColor(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    void Context::SetShadowColor(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    Napi::Value Context::GetShadowBlur(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    void Context::SetShadowBlur(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    Napi::Value Context::GetShadowOffsetX(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    void Context::SetShadowOffsetX(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    Napi::Value Context::GetShadowOffsetY(const Napi::CallbackInfo& info)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }

    void Context::SetShadowOffsetY(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        throw Napi::Error::New(info.Env(), "not implemented");
    }
}
