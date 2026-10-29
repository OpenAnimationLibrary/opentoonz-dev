#include "winhandwritingrecognizer.h"

#include <QMutexLocker>
#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleauto.h>

namespace {

// Use the same Windows Ink Automation API as the working Python demo. Late
// binding also works with Windows toolchains whose msinkaut.h is incomplete.
const GUID RecognizersId = {0x9fd4e808,
                            0xf6e6,
                            0x4e65,
                            {0x98, 0xd3, 0xaa, 0x39, 0x05, 0x4c, 0x12, 0x55}};
const GUID InkId         = {0x937c1a34,
                            0x151d,
                            0x4610,
                            {0x9c, 0xa6, 0xa8, 0xcc, 0x9b, 0xdb, 0x5d, 0x83}};
const GUID DispatchId    = {0x00020400, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
const GUID NullId        = {};

struct Variant {
  VARIANT value;
  Variant() { VariantInit(&value); }
  ~Variant() { VariantClear(&value); }
  Variant(const Variant &)            = delete;
  Variant &operator=(const Variant &) = delete;
};

HRESULT createInkObject(const GUID &id, Variant &object) {
  IDispatch *dispatch = nullptr;
  HRESULT hr = CoCreateInstance(id, nullptr, CLSCTX_INPROC_SERVER, DispatchId,
                                (void **)&dispatch);
  if (SUCCEEDED(hr)) {
    object.value.vt       = VT_DISPATCH;
    object.value.pdispVal = dispatch;
  }
  return hr;
}

HRESULT invoke(const Variant &object, const wchar_t *name, WORD flags,
               VARIANT *args = nullptr, UINT count = 0,
               Variant *result = nullptr) {
  if (object.value.vt != VT_DISPATCH || !object.value.pdispVal)
    return E_NOINTERFACE;
  DISPID id;
  LPOLESTR member = const_cast<LPOLESTR>(name);
  HRESULT hr      = object.value.pdispVal->GetIDsOfNames(NullId, &member, 1,
                                                         LOCALE_USER_DEFAULT, &id);
  if (FAILED(hr)) return hr;
  DISPID named      = DISPID_PROPERTYPUT;
  DISPPARAMS params = {args, nullptr, count, 0};
  if (flags == DISPATCH_PROPERTYPUTREF) {
    params.rgdispidNamedArgs = &named;
    params.cNamedArgs        = 1;
  }
  EXCEPINFO exception = {};
  UINT argumentError  = 0;
  hr = object.value.pdispVal->Invoke(id, NullId, LOCALE_USER_DEFAULT, flags,
                                     &params, result ? &result->value : nullptr,
                                     &exception, &argumentError);
  SysFreeString(exception.bstrSource);
  SysFreeString(exception.bstrDescription);
  SysFreeString(exception.bstrHelpFile);
  return hr;
}

VARIANT integer(LONG value) {
  VARIANT result;
  VariantInit(&result);
  result.vt   = VT_I4;
  result.lVal = value;
  return result;
}

QString failure(HRESULT hr) {
  return QObject::tr("Windows handwriting recognition failed (0x%1).")
      .arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

QString recognizeInk(const Variant &ink, const Variant &recognizer,
                     const QVector<QPolygonF> &strokes, QString &error) {
  Variant oldStrokes, count;
  HRESULT hr =
      invoke(ink, L"Strokes", DISPATCH_PROPERTYGET, nullptr, 0, &oldStrokes);
  if (SUCCEEDED(hr))
    hr = invoke(oldStrokes, L"Count", DISPATCH_PROPERTYGET, nullptr, 0, &count);
  if (SUCCEEDED(hr) && count.value.vt == VT_I4 && count.value.lVal)
    hr = invoke(ink, L"DeleteStrokes", DISPATCH_METHOD, &oldStrokes.value, 1);
  for (const QPolygonF &points : strokes) {
    if (FAILED(hr)) break;
    if (points.isEmpty()) continue;
    const int pointCount = qMax(2, points.size());
    Variant data, stroke;
    data.value.vt     = VT_ARRAY | VT_I4;
    data.value.parray = SafeArrayCreateVector(VT_I4, 0, pointCount * 2);
    if (!data.value.parray) {
      hr = E_OUTOFMEMORY;
      break;
    }
    LONG *packets = nullptr;
    hr            = SafeArrayAccessData(data.value.parray, (void **)&packets);
    if (SUCCEEDED(hr)) {
      for (int i = 0; i < pointCount; ++i) {
        const QPointF point = points[qMin(i, points.size() - 1)];
        // Qt logical pixels (96 dpi) -> Windows ink units (.01 mm).
        packets[2 * i]     = LONG(std::lround(point.x() * 2540.0 / 96.0));
        packets[2 * i + 1] = LONG(std::lround(point.y() * 2540.0 / 96.0));
      }
      if (points.size() == 1) ++packets[2];  // Preserve pen taps/dots.
      SafeArrayUnaccessData(data.value.parray);
      // Automation arguments are reversed: reserved description, then packets.
      VARIANT args[2];
      VariantInit(&args[0]);
      args[1] = data.value;  // Borrowed; data owns the SafeArray.
      hr      = invoke(ink, L"CreateStroke", DISPATCH_METHOD, args, 2, &stroke);
    }
  }
  Variant context, inkStrokes, result;
  if (SUCCEEDED(hr))
    hr = invoke(recognizer, L"CreateRecognizerContext", DISPATCH_METHOD,
                nullptr, 0, &context);
  if (SUCCEEDED(hr))
    hr = invoke(ink, L"Strokes", DISPATCH_PROPERTYGET, nullptr, 0, &inkStrokes);
  if (SUCCEEDED(hr))
    hr = invoke(context, L"Strokes", DISPATCH_PROPERTYPUTREF, &inkStrokes.value,
                1);
  LONG status = 0;
  VARIANT statusArgument;
  VariantInit(&statusArgument);
  statusArgument.vt    = VT_I4 | VT_BYREF;
  statusArgument.plVal = &status;
  if (SUCCEEDED(hr))
    hr = invoke(context, L"Recognize", DISPATCH_METHOD, &statusArgument, 1,
                &result);
  Variant text;
  if (SUCCEEDED(hr) && status == 0)
    hr = invoke(result, L"TopString", DISPATCH_PROPERTYGET, nullptr, 0, &text);
  if (FAILED(hr) || status != 0) {
    error = FAILED(hr)
                ? failure(hr)
                : QObject::tr("Windows could not recognize this writing.");
    return QString();
  }
  return text.value.vt == VT_BSTR
             ? QString::fromWCharArray(text.value.bstrVal,
                                       SysStringLen(text.value.bstrVal))
             : QString();
}

}  // namespace

WinHandwritingRecognizer::WinHandwritingRecognizer(QObject *parent)
    : QThread(parent) {
  connect(this, &QThread::finished, this, [this] {
    if (!m_restarting) return;
    {
      QMutexLocker lock(&m_mutex);
      m_restarting = m_stopping = m_pending = false;
    }
    start();
  });
}

WinHandwritingRecognizer::~WinHandwritingRecognizer() {
  {
    QMutexLocker lock(&m_mutex);
    m_restarting = false;
    m_stopping   = true;
    m_wake.wakeOne();
  }
  wait();
}

void WinHandwritingRecognizer::recognize(const QVector<QPolygonF> &strokes,
                                         int recognizer, quint64 revision) {
  QMutexLocker lock(&m_mutex);
  m_strokes    = strokes;
  m_recognizer = recognizer;
  m_revision   = revision;
  m_pending    = true;
  m_wake.wakeOne();
}

void WinHandwritingRecognizer::cancelPending() {
  QMutexLocker lock(&m_mutex);
  m_pending = false;
  m_strokes.clear();
}

void WinHandwritingRecognizer::recheck() {
  if (!isRunning()) {
    start();
    return;
  }
  QMutexLocker lock(&m_mutex);
  m_restarting = true;
  m_stopping   = true;
  m_pending    = false;
  m_wake.wakeOne();
}

void WinHandwritingRecognizer::run() {
  HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized)) {
    emit recognized(0, QString(), failure(initialized));
    return;
  }
  {
    Variant collection, ink, count;
    HRESULT hr = createInkObject(RecognizersId, collection);
    if (SUCCEEDED(hr)) hr = createInkObject(InkId, ink);
    if (SUCCEEDED(hr))
      hr = invoke(collection, L"Count", DISPATCH_PROPERTYGET, nullptr, 0,
                  &count);
    QStringList names;
    QVector<int> indices;
    const LONG size = count.value.vt == VT_I4 ? count.value.lVal : 0;
    for (LONG i = 0; SUCCEEDED(hr) && i < size; ++i) {
      Variant item, languages, name;
      VARIANT index = integer(i);
      hr = invoke(collection, L"Item", DISPATCH_METHOD, &index, 1, &item);
      if (SUCCEEDED(hr))
        hr = invoke(item, L"Languages", DISPATCH_PROPERTYGET, nullptr, 0,
                    &languages);
      LONG first = 0, last = -1;
      if (SUCCEEDED(hr) && (languages.value.vt & VT_ARRAY) &&
          languages.value.parray) {
        SafeArrayGetLBound(languages.value.parray, 1, &first);
        SafeArrayGetUBound(languages.value.parray, 1, &last);
      }
      if (last < first) continue;  // Skip gesture-only recognizers.
      hr = invoke(item, L"Name", DISPATCH_PROPERTYGET, nullptr, 0, &name);
      if (SUCCEEDED(hr) && name.value.vt == VT_BSTR) {
        names.append(QString::fromWCharArray(name.value.bstrVal,
                                             SysStringLen(name.value.bstrVal)));
        indices.append(i);
      }
    }
    if (FAILED(hr)) names.clear();
    emit recognizersReady(names);
    if (FAILED(hr) || names.isEmpty()) {
      emit recognized(
          0, QString(),
          tr("No Windows handwriting recognizer is available. "
             "Install Handwriting in Windows language options, then "
             "click Recheck Windows."));
    } else {
      for (;;) {
        QVector<QPolygonF> strokes;
        quint64 revision;
        int selected;
        {
          QMutexLocker lock(&m_mutex);
          while (!m_stopping && !m_pending) m_wake.wait(&m_mutex);
          if (m_stopping) break;
          strokes   = m_strokes;
          revision  = m_revision;
          selected  = m_recognizer;
          m_pending = false;
        }
        if (selected < 0 || selected >= indices.size()) continue;
        Variant item;
        VARIANT index = integer(indices[selected]);
        hr = invoke(collection, L"Item", DISPATCH_METHOD, &index, 1, &item);
        QString error, text;
        if (SUCCEEDED(hr))
          text = recognizeInk(ink, item, strokes, error);
        else
          error = failure(hr);
        emit recognized(revision, text, error);
      }
    }
  }  // Release all COM objects before uninitializing the apartment.
  CoUninitialize();
}
