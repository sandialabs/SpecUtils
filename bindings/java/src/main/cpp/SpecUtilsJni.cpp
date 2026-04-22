// JNI glue between gov.sandia.specutils.internal.Native and the C binding
// layer in bindings/c/SpecUtils_c.h. Each function here is a thin
// Java<->C type translator — no business logic, so the attack surface for
// version-specific JVM quirks is as small as possible.
//
// Generated header (produced at build time by `javac -h`):
#include "gov_sandia_specutils_internal_Native.h"

#include <jni.h>
#include <cstring>
#include <vector>
#include <string>

#include "SpecUtils_c.h"

// =============================================================================
//  Helpers
// =============================================================================

namespace {

// Cast the Java long-sized handle back to the appropriate C type.
template<typename T>
inline T* handle_as(jlong h) {
    return reinterpret_cast<T*>(static_cast<intptr_t>(h));
}

inline jlong to_handle(const void *p) {
    return static_cast<jlong>(reinterpret_cast<intptr_t>(p));
}

// Throw java.lang.IllegalStateException — used when Java passes a 0 handle
// into a native method, which means the wrapper has already been closed.
void throw_closed(JNIEnv *env, const char *what) {
    jclass cls = env->FindClass("java/lang/IllegalStateException");
    if (cls) env->ThrowNew(cls, what);
}

// Throw gov.sandia.specutils.SpecUtilsException with the given message.
void throw_specutils(JNIEnv *env, const char *msg) {
    jclass cls = env->FindClass("gov/sandia/specutils/SpecUtilsException");
    if (cls) env->ThrowNew(cls, msg);
}

// Return true if handle is 0 (and throw IllegalStateException so Java sees it).
inline bool check_handle(JNIEnv *env, jlong h, const char *what) {
    if (h == 0) { throw_closed(env, what); return true; }
    return false;
}

// Scoped helper for jstring -> UTF-8 char* conversion. Release happens in dtor.
class JStringUtf8 {
public:
    JStringUtf8(JNIEnv *env, jstring js)
        : env_(env), js_(js), chars_(nullptr) {
        if (js) chars_ = env->GetStringUTFChars(js, nullptr);
    }
    ~JStringUtf8() {
        if (chars_) env_->ReleaseStringUTFChars(js_, chars_);
    }
    const char* c_str() const { return chars_; }
    explicit operator bool() const { return chars_ != nullptr; }

    JStringUtf8(const JStringUtf8&) = delete;
    JStringUtf8& operator=(const JStringUtf8&) = delete;

private:
    JNIEnv *env_;
    jstring js_;
    const char *chars_;
};

// Convert const char* -> jstring; NULL input returns NULL jstring.
inline jstring to_jstring(JNIEnv *env, const char *s) {
    if (!s) return nullptr;
    return env->NewStringUTF(s);
}

// Convert a Java String[] to a vector of UTF-8 C strings. Pointer-to-c_str
// storage is returned via out_ptrs (stable for the life of the container).
class JStringArrayUtf8 {
public:
    JStringArrayUtf8(JNIEnv *env, jobjectArray arr) {
        if (!arr) return;
        jsize n = env->GetArrayLength(arr);
        owned_.reserve(n);
        ptrs_.reserve(n);
        for (jsize i = 0; i < n; i++) {
            jstring js = (jstring) env->GetObjectArrayElement(arr, i);
            if (js) {
                const char *c = env->GetStringUTFChars(js, nullptr);
                owned_.emplace_back(c ? c : "");
                if (c) env->ReleaseStringUTFChars(js, c);
            } else {
                owned_.emplace_back();
            }
            env->DeleteLocalRef(js);
        }
        for (auto &s : owned_) ptrs_.push_back(s.c_str());
    }
    const char** data() { return ptrs_.empty() ? nullptr : ptrs_.data(); }
    uint32_t size() const { return static_cast<uint32_t>(ptrs_.size()); }
private:
    std::vector<std::string> owned_;
    std::vector<const char*> ptrs_;
};

// Helper: build a Java float[] from a C float* + length. count==0 or data==null
// returns an empty float[] (never null).
jfloatArray make_jfloat_array(JNIEnv *env, const float *data, uint32_t count) {
    jfloatArray arr = env->NewFloatArray(static_cast<jsize>(count));
    if (count > 0 && data && arr) {
        env->SetFloatArrayRegion(arr, 0, static_cast<jsize>(count), data);
    }
    return arr;
}

} // namespace

// =============================================================================
//  SpecFile lifecycle
// =============================================================================

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_specFileCreate
  (JNIEnv *, jclass) {
    return to_handle(SpecUtils_SpecFile_create());
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_specFileClone
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "specFileClone")) return 0;
    return to_handle(SpecUtils_SpecFile_clone(handle_as<SpecUtils_SpecFile>(h)));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_specFileDestroy
  (JNIEnv *, jclass, jlong h) {
    if (h == 0) return;
    SpecUtils_SpecFile_destroy(handle_as<SpecUtils_SpecFile>(h));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetEqual
  (JNIEnv *env, jclass, jlong lhs, jlong rhs) {
    if (check_handle(env, lhs, "specFileSetEqual lhs")) return;
    if (check_handle(env, rhs, "specFileSetEqual rhs")) return;
    SpecUtils_SpecFile_set_equal(handle_as<SpecUtils_SpecFile>(lhs),
                                 handle_as<SpecUtils_SpecFile>(rhs));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_specFileReset
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "specFileReset")) return;
    SpecUtils_SpecFile_reset(handle_as<SpecUtils_SpecFile>(h));
}

// =============================================================================
//  SpecFile parsing & writing
// =============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileLoadFile
  (JNIEnv *env, jclass, jlong h, jstring jfilename) {
    if (check_handle(env, h, "specFileLoadFile")) return JNI_FALSE;
    JStringUtf8 fn(env, jfilename);
    if (!fn) { throw_specutils(env, "filename null"); return JNI_FALSE; }
    bool ok = SpecFile_load_file(handle_as<SpecUtils_SpecFile>(h), fn.c_str());
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileLoadFileFromFormat
  (JNIEnv *env, jclass, jlong h, jstring jfilename, jint parserOrdinal) {
    if (check_handle(env, h, "specFileLoadFileFromFormat")) return JNI_FALSE;
    JStringUtf8 fn(env, jfilename);
    if (!fn) { throw_specutils(env, "filename null"); return JNI_FALSE; }
    bool ok = SpecFile_load_file_from_format(
        handle_as<SpecUtils_SpecFile>(h),
        fn.c_str(),
        static_cast<SpecUtils_ParserType>(parserOrdinal));
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileWriteToFile
  (JNIEnv *env, jclass, jlong h, jstring jfilename, jint saveOrdinal) {
    if (check_handle(env, h, "specFileWriteToFile")) return JNI_FALSE;
    JStringUtf8 fn(env, jfilename);
    if (!fn) { throw_specutils(env, "filename null"); return JNI_FALSE; }
    bool ok = SpecUtils_write_to_file(
        handle_as<SpecUtils_SpecFile>(h),
        fn.c_str(),
        static_cast<SpecUtils_SaveSpectrumAsType>(saveOrdinal));
    return ok ? JNI_TRUE : JNI_FALSE;
}

// =============================================================================
//  SpecFile queries
// =============================================================================

#define SPECFILE_BOOL_GETTER(jname, cname)                                      \
extern "C" JNIEXPORT jboolean JNICALL                                           \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return JNI_FALSE;                         \
    return cname(handle_as<SpecUtils_SpecFile>(h)) ? JNI_TRUE : JNI_FALSE;      \
}

#define SPECFILE_INT_GETTER(jname, cname)                                       \
extern "C" JNIEXPORT jint JNICALL                                               \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return 0;                                 \
    return static_cast<jint>(cname(handle_as<SpecUtils_SpecFile>(h)));          \
}

#define SPECFILE_FLT_GETTER(jname, cname)                                       \
extern "C" JNIEXPORT jfloat JNICALL                                             \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return 0.0f;                              \
    return cname(handle_as<SpecUtils_SpecFile>(h));                             \
}

#define SPECFILE_DBL_GETTER(jname, cname)                                       \
extern "C" JNIEXPORT jdouble JNICALL                                            \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return 0.0;                               \
    return cname(handle_as<SpecUtils_SpecFile>(h));                             \
}

#define SPECFILE_STR_GETTER(jname, cname)                                       \
extern "C" JNIEXPORT jstring JNICALL                                            \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return nullptr;                           \
    return to_jstring(env, cname(handle_as<SpecUtils_SpecFile>(h)));            \
}

SPECFILE_BOOL_GETTER(specFilePassthrough,            SpecUtils_SpecFile_passthrough)
SPECFILE_INT_GETTER (specFileNumberMeasurements,     SpecUtils_SpecFile_number_measurements)
SPECFILE_INT_GETTER (specFileNumberGammaChannels,    SpecUtils_SpecFile_number_gamma_channels)
SPECFILE_BOOL_GETTER(specFileModified,               SpecUtils_SpecFile_modified)
SPECFILE_INT_GETTER (specFileMemorySize,             SpecUtils_SpecFile_memmorysize)
SPECFILE_INT_GETTER (specFileNumberDetectors,        SpecUtils_SpecFile_number_detectors)
SPECFILE_INT_GETTER (specFileNumberGammaDetectors,   SpecUtils_SpecFile_number_gamma_detectors)
SPECFILE_INT_GETTER (specFileNumberNeutronDetectors, SpecUtils_SpecFile_number_neutron_detectors)
SPECFILE_INT_GETTER (specFileNumberSamples,          SpecUtils_SpecFile_number_samples)
SPECFILE_INT_GETTER (specFileNumberRemarks,          SpecUtils_SpecFile_number_remarks)
SPECFILE_INT_GETTER (specFileNumberParseWarnings,    SpecUtils_SpecFile_number_parse_warnings)
SPECFILE_FLT_GETTER (specFileSumGammaLiveTime,       SpecUtils_SpecFile_sum_gamma_live_time)
SPECFILE_FLT_GETTER (specFileSumGammaRealTime,       SpecUtils_SpecFile_sum_gamma_real_time)
SPECFILE_DBL_GETTER (specFileGammaCountSum,          SpecUtils_SpecFile_gamma_count_sum)
SPECFILE_DBL_GETTER (specFileNeutronCountsSum,       SpecUtils_SpecFile_neutron_counts_sum)
SPECFILE_STR_GETTER (specFileFilename,               SpecUtils_SpecFile_filename)
SPECFILE_STR_GETTER (specFileUuid,                   SpecUtils_SpecFile_uuid)
SPECFILE_STR_GETTER (specFileMeasurementLocationName, SpecUtils_SpecFile_measurement_location_name)
SPECFILE_STR_GETTER (specFileMeasurementOperator,    SpecUtils_SpecFile_measurement_operator)
SPECFILE_STR_GETTER (specFileInstrumentType,         SpecUtils_SpecFile_instrument_type)
SPECFILE_STR_GETTER (specFileManufacturer,           SpecUtils_SpecFile_manufacturer)
SPECFILE_STR_GETTER (specFileInstrumentModel,        SpecUtils_SpecFile_instrument_model)
SPECFILE_STR_GETTER (specFileInstrumentId,           SpecUtils_SpecFile_instrument_id)
SPECFILE_BOOL_GETTER(specFileHasGpsInfo,             SpecUtils_SpecFile_has_gps_info)
SPECFILE_DBL_GETTER (specFileMeanLatitude,           SpecUtils_SpecFile_mean_latitude)
SPECFILE_DBL_GETTER (specFileMeanLongitude,          SpecUtils_SpecFile_mean_longitude)
SPECFILE_BOOL_GETTER(specFileContainsDerivedData,    SpecUtils_SpecFile_contains_derived_data)
SPECFILE_BOOL_GETTER(specFileContainsNonDerivedData, SpecUtils_SpecFile_contains_non_derived_data)

extern "C" JNIEXPORT jint JNICALL
Java_gov_sandia_specutils_internal_Native_specFileDetectorType
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "specFileDetectorType")) return 0;
    return static_cast<jint>(SpecUtils_SpecFile_detector_type(handle_as<SpecUtils_SpecFile>(h)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_specFileGetMeasurementByIndex
  (JNIEnv *env, jclass, jlong h, jint index) {
    if (check_handle(env, h, "specFileGetMeasurementByIndex")) return 0;
    return to_handle(SpecUtils_SpecFile_get_measurement_by_index(
        handle_as<SpecUtils_SpecFile>(h), static_cast<uint32_t>(index)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_specFileGetMeasurementBySampleDet
  (JNIEnv *env, jclass, jlong h, jint sampleNumber, jstring jdet) {
    if (check_handle(env, h, "specFileGetMeasurementBySampleDet")) return 0;
    JStringUtf8 det(env, jdet);
    return to_handle(SpecUtils_SpecFile_get_measurement_by_sample_det(
        handle_as<SpecUtils_SpecFile>(h), sampleNumber, det.c_str()));
}

extern "C" JNIEXPORT jstring JNICALL
Java_gov_sandia_specutils_internal_Native_specFileDetectorName
  (JNIEnv *env, jclass, jlong h, jint index) {
    if (check_handle(env, h, "specFileDetectorName")) return nullptr;
    return to_jstring(env, SpecUtils_SpecFile_detector_name(
        handle_as<SpecUtils_SpecFile>(h), static_cast<uint32_t>(index)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_gov_sandia_specutils_internal_Native_specFileGammaDetectorName
  (JNIEnv *env, jclass, jlong h, jint index) {
    if (check_handle(env, h, "specFileGammaDetectorName")) return nullptr;
    return to_jstring(env, SpecUtils_SpecFile_gamma_detector_name(
        handle_as<SpecUtils_SpecFile>(h), static_cast<uint32_t>(index)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_gov_sandia_specutils_internal_Native_specFileNeutronDetectorName
  (JNIEnv *env, jclass, jlong h, jint index) {
    if (check_handle(env, h, "specFileNeutronDetectorName")) return nullptr;
    return to_jstring(env, SpecUtils_SpecFile_neutron_detector_name(
        handle_as<SpecUtils_SpecFile>(h), static_cast<uint32_t>(index)));
}

extern "C" JNIEXPORT jint JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSampleNumber
  (JNIEnv *env, jclass, jlong h, jint index) {
    if (check_handle(env, h, "specFileSampleNumber")) return 0;
    return SpecUtils_SpecFile_sample_number(
        handle_as<SpecUtils_SpecFile>(h), static_cast<uint32_t>(index));
}

extern "C" JNIEXPORT jstring JNICALL
Java_gov_sandia_specutils_internal_Native_specFileRemark
  (JNIEnv *env, jclass, jlong h, jint idx) {
    if (check_handle(env, h, "specFileRemark")) return nullptr;
    return to_jstring(env, SpecUtils_SpecFile_remark(
        handle_as<SpecUtils_SpecFile>(h), static_cast<uint32_t>(idx)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_gov_sandia_specutils_internal_Native_specFileParseWarning
  (JNIEnv *env, jclass, jlong h, jint idx) {
    if (check_handle(env, h, "specFileParseWarning")) return nullptr;
    return to_jstring(env, SpecUtils_SpecFile_parse_warning(
        handle_as<SpecUtils_SpecFile>(h), static_cast<uint32_t>(idx)));
}

// =============================================================================
//  SpecFile mutators
// =============================================================================

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSumMeasurements
  (JNIEnv *env, jclass, jlong h, jintArray jsamples, jobjectArray jdets) {
    if (check_handle(env, h, "specFileSumMeasurements")) return 0;
    jsize ns = jsamples ? env->GetArrayLength(jsamples) : 0;
    std::vector<jint> samples(ns);
    if (ns > 0) env->GetIntArrayRegion(jsamples, 0, ns, samples.data());
    std::vector<int> samples_i(samples.begin(), samples.end());

    JStringArrayUtf8 dets(env, jdets);
    return to_handle(SpecUtils_SpecFile_sum_measurements(
        handle_as<SpecUtils_SpecFile>(h),
        samples_i.data(), static_cast<uint32_t>(samples_i.size()),
        dets.data(), dets.size()));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileAddMeasurement
  (JNIEnv *env, jclass, jlong h, jlong mh, jboolean cleanup) {
    if (check_handle(env, h, "specFileAddMeasurement")) return JNI_FALSE;
    if (check_handle(env, mh, "specFileAddMeasurement measurement")) return JNI_FALSE;
    return SpecUtils_SpecFile_add_measurement(
        handle_as<SpecUtils_SpecFile>(h),
        handle_as<SpecUtils_Measurement>(mh),
        cleanup == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileRemoveMeasurement
  (JNIEnv *env, jclass, jlong h, jlong mh, jboolean cleanup) {
    if (check_handle(env, h, "specFileRemoveMeasurement")) return JNI_FALSE;
    if (check_handle(env, mh, "specFileRemoveMeasurement measurement")) return JNI_FALSE;
    return SpecUtils_SpecFile_remove_measurement(
        handle_as<SpecUtils_SpecFile>(h),
        handle_as<SpecUtils_Measurement>(mh),
        cleanup == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileRemoveMeasurements
  (JNIEnv *env, jclass, jlong h, jlongArray jhandles) {
    if (check_handle(env, h, "specFileRemoveMeasurements")) return JNI_FALSE;
    jsize n = jhandles ? env->GetArrayLength(jhandles) : 0;
    std::vector<jlong> handles(n);
    if (n > 0) env->GetLongArrayRegion(jhandles, 0, n, handles.data());
    std::vector<const SpecUtils_Measurement*> mptrs(n);
    for (jsize i = 0; i < n; i++) {
        mptrs[i] = handle_as<SpecUtils_Measurement>(handles[i]);
    }
    return SpecUtils_SpecFile_remove_measurements(
        handle_as<SpecUtils_SpecFile>(h),
        mptrs.data(), static_cast<uint32_t>(n)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_specFileCleanup
  (JNIEnv *env, jclass, jlong h, jboolean dontChange, jboolean reorder) {
    if (check_handle(env, h, "specFileCleanup")) return;
    SpecUtils_SpecFile_cleanup(handle_as<SpecUtils_SpecFile>(h),
                               dontChange == JNI_TRUE, reorder == JNI_TRUE);
}

#define SPECFILE_STR_SETTER(jname, cname)                                         \
extern "C" JNIEXPORT void JNICALL                                                 \
Java_gov_sandia_specutils_internal_Native_##jname                                 \
  (JNIEnv *env, jclass, jlong h, jstring js) {                                    \
    if (check_handle(env, h, #jname)) return;                                     \
    JStringUtf8 s(env, js);                                                       \
    cname(handle_as<SpecUtils_SpecFile>(h), s.c_str());                           \
}

SPECFILE_STR_SETTER(specFileSetFilename,                SpecUtils_SpecFile_set_filename)
SPECFILE_STR_SETTER(specFileAddRemark,                  SpecUtils_SpecFile_add_remark)
SPECFILE_STR_SETTER(specFileSetUuid,                    SpecUtils_SpecFile_set_uuid)
SPECFILE_STR_SETTER(specFileSetMeasurementLocationName, SpecUtils_SpecFile_set_measurement_location_name)
SPECFILE_STR_SETTER(specFileSetInspection,              SpecUtils_SpecFile_set_inspection)
SPECFILE_STR_SETTER(specFileSetInstrumentType,          SpecUtils_SpecFile_set_instrument_type)
SPECFILE_STR_SETTER(specFileSetManufacturer,            SpecUtils_SpecFile_set_manufacturer)
SPECFILE_STR_SETTER(specFileSetInstrumentModel,         SpecUtils_SpecFile_set_instrument_model)
SPECFILE_STR_SETTER(specFileSetInstrumentId,            SpecUtils_SpecFile_set_instrument_id)

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetRemarks
  (JNIEnv *env, jclass, jlong h, jobjectArray jarr) {
    if (check_handle(env, h, "specFileSetRemarks")) return;
    JStringArrayUtf8 arr(env, jarr);
    SpecUtils_SpecFile_set_remarks(handle_as<SpecUtils_SpecFile>(h),
                                   arr.data(), arr.size());
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetParseWarnings
  (JNIEnv *env, jclass, jlong h, jobjectArray jarr) {
    if (check_handle(env, h, "specFileSetParseWarnings")) return;
    JStringArrayUtf8 arr(env, jarr);
    SpecUtils_SpecFile_set_parse_warnings(handle_as<SpecUtils_SpecFile>(h),
                                          arr.data(), arr.size());
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetLaneNumber
  (JNIEnv *env, jclass, jlong h, jint n) {
    if (check_handle(env, h, "specFileSetLaneNumber")) return;
    SpecUtils_SpecFile_set_lane_number(handle_as<SpecUtils_SpecFile>(h), n);
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetDetectorType
  (JNIEnv *env, jclass, jlong h, jint ord) {
    if (check_handle(env, h, "specFileSetDetectorType")) return;
    SpecUtils_SpecFile_set_detector_type(handle_as<SpecUtils_SpecFile>(h),
        static_cast<SpecUtils_DetectorType>(ord));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileChangeDetectorName
  (JNIEnv *env, jclass, jlong h, jstring joriginal, jstring jnewname) {
    if (check_handle(env, h, "specFileChangeDetectorName")) return JNI_FALSE;
    JStringUtf8 o(env, joriginal), n(env, jnewname);
    return SpecUtils_SpecFile_change_detector_name(handle_as<SpecUtils_SpecFile>(h),
        o.c_str(), n.c_str()) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetEnergyCalibrationFromCALpFile
  (JNIEnv *env, jclass, jlong h, jstring jpath) {
    if (check_handle(env, h, "specFileSetEnergyCalibrationFromCALpFile")) return JNI_FALSE;
    JStringUtf8 p(env, jpath);
    return set_energy_calibration_from_CALp_file(handle_as<SpecUtils_SpecFile>(h),
        p.c_str()) ? JNI_TRUE : JNI_FALSE;
}

// Per-measurement setters (only valid for measurements owned by the SpecFile)

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementLiveTime
  (JNIEnv *env, jclass, jlong h, jfloat t, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementLiveTime")) return JNI_FALSE;
    return SpecUtils_SpecFile_set_measurement_live_time(
        handle_as<SpecUtils_SpecFile>(h), t,
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementRealTime
  (JNIEnv *env, jclass, jlong h, jfloat t, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementRealTime")) return JNI_FALSE;
    return SpecUtils_SpecFile_set_measurement_real_time(
        handle_as<SpecUtils_SpecFile>(h), t,
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementStartTimeUsecs
  (JNIEnv *env, jclass, jlong h, jlong usecs, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementStartTimeUsecs")) return JNI_FALSE;
    return SpecUtils_SpecFile_set_measurement_start_time(
        handle_as<SpecUtils_SpecFile>(h), usecs,
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementStartTimeStr
  (JNIEnv *env, jclass, jlong h, jstring jdt, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementStartTimeStr")) return JNI_FALSE;
    JStringUtf8 dt(env, jdt);
    return SpecUtils_SpecFile_set_measurement_start_time_str(
        handle_as<SpecUtils_SpecFile>(h), dt.c_str(),
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementRemarks
  (JNIEnv *env, jclass, jlong h, jobjectArray jarr, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementRemarks")) return JNI_FALSE;
    JStringArrayUtf8 arr(env, jarr);
    return SpecUtils_SpecFile_set_measurement_remarks(
        handle_as<SpecUtils_SpecFile>(h), arr.data(), arr.size(),
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementSourceType
  (JNIEnv *env, jclass, jlong h, jint ord, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementSourceType")) return JNI_FALSE;
    return SpecUtils_SpecFile_set_measurement_source_type(
        handle_as<SpecUtils_SpecFile>(h),
        static_cast<SpecUtils_SourceType>(ord),
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementPosition
  (JNIEnv *env, jclass, jlong h, jdouble lon, jdouble lat, jlong usecs, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementPosition")) return JNI_FALSE;
    return SpecUtils_SpecFile_set_measurement_position(
        handle_as<SpecUtils_SpecFile>(h), lon, lat, usecs,
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementTitle
  (JNIEnv *env, jclass, jlong h, jstring jtitle, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementTitle")) return JNI_FALSE;
    JStringUtf8 t(env, jtitle);
    return SpecUtils_SpecFile_set_measurement_title(
        handle_as<SpecUtils_SpecFile>(h), t.c_str(),
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementContainedNeutrons
  (JNIEnv *env, jclass, jlong h, jboolean contained, jfloat counts,
   jfloat nlt, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementContainedNeutrons")) return JNI_FALSE;
    return SpecUtils_SpecFile_set_measurement_contained_neutrons(
        handle_as<SpecUtils_SpecFile>(h),
        contained == JNI_TRUE, counts, nlt,
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_specFileSetMeasurementEnergyCalibration
  (JNIEnv *env, jclass, jlong h, jlong refH, jlong mh) {
    if (check_handle(env, h, "specFileSetMeasurementEnergyCalibration")) return JNI_FALSE;
    if (check_handle(env, refH, "specFileSetMeasurementEnergyCalibration ref")) return JNI_FALSE;
    return SpecUtils_SpecFile_set_measurement_energy_calibration(
        handle_as<SpecUtils_SpecFile>(h),
        handle_as<SpecUtils_CountedRef_EnergyCal>(refH),
        handle_as<SpecUtils_Measurement>(mh)) ? JNI_TRUE : JNI_FALSE;
}

// =============================================================================
//  Measurement
// =============================================================================

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_measurementCreate(JNIEnv *, jclass) {
    return to_handle(SpecUtils_Measurement_create());
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_measurementClone
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementClone")) return 0;
    return to_handle(SpecUtils_Measurement_clone(handle_as<SpecUtils_Measurement>(h)));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementDestroy
  (JNIEnv *, jclass, jlong h) {
    if (h == 0) return;
    SpecUtils_Measurement_destroy(handle_as<SpecUtils_Measurement>(h));
}

extern "C" JNIEXPORT jint JNICALL
Java_gov_sandia_specutils_internal_Native_measurementMemorySize
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementMemorySize")) return 0;
    return static_cast<jint>(SpecUtils_Measurement_memmorysize(
        handle_as<SpecUtils_Measurement>(h)));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetEqual
  (JNIEnv *env, jclass, jlong lhs, jlong rhs) {
    if (check_handle(env, lhs, "measurementSetEqual")) return;
    if (check_handle(env, rhs, "measurementSetEqual rhs")) return;
    SpecUtils_Measurement_set_equal(handle_as<SpecUtils_Measurement>(lhs),
                                    handle_as<SpecUtils_Measurement>(rhs));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementReset
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementReset")) return;
    SpecUtils_Measurement_reset(handle_as<SpecUtils_Measurement>(h));
}

// ---- Measurement string getters/setters ----

#define MEAS_STR_GETTER(jname, cname)                                            \
extern "C" JNIEXPORT jstring JNICALL                                             \
Java_gov_sandia_specutils_internal_Native_##jname                                \
  (JNIEnv *env, jclass, jlong h) {                                               \
    if (check_handle(env, h, #jname)) return nullptr;                            \
    return to_jstring(env, cname(handle_as<SpecUtils_Measurement>(h)));          \
}

#define MEAS_STR_SETTER(jname, cname)                                            \
extern "C" JNIEXPORT void JNICALL                                                \
Java_gov_sandia_specutils_internal_Native_##jname                                \
  (JNIEnv *env, jclass, jlong h, jstring js) {                                   \
    if (check_handle(env, h, #jname)) return;                                    \
    JStringUtf8 s(env, js);                                                      \
    cname(handle_as<SpecUtils_Measurement>(h), s.c_str());                       \
}

MEAS_STR_GETTER(measurementDescription,  SpecUtils_Measurement_description)
MEAS_STR_SETTER(measurementSetDescription, SpecUtils_Measurement_set_description)
MEAS_STR_GETTER(measurementSourceString, SpecUtils_Measurement_source_string)
MEAS_STR_SETTER(measurementSetSourceString, SpecUtils_Measurement_set_source_string)
MEAS_STR_GETTER(measurementTitle,        SpecUtils_Measurement_title)
MEAS_STR_SETTER(measurementSetTitle,     SpecUtils_Measurement_set_title)
MEAS_STR_GETTER(measurementDetectorName, SpecUtils_Measurement_detector_name)
MEAS_STR_SETTER(measurementSetDetectorName, SpecUtils_Measurement_set_detector_name)
MEAS_STR_GETTER(measurementDetectorType, SpecUtils_Measurement_detector_type)

// ---- Measurement numeric getters ----

#define MEAS_FLT_GETTER(jname, cname)                                           \
extern "C" JNIEXPORT jfloat JNICALL                                             \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return 0.0f;                              \
    return cname(handle_as<SpecUtils_Measurement>(h));                          \
}

#define MEAS_DBL_GETTER(jname, cname)                                           \
extern "C" JNIEXPORT jdouble JNICALL                                            \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return 0.0;                               \
    return cname(handle_as<SpecUtils_Measurement>(h));                          \
}

#define MEAS_BOOL_GETTER(jname, cname)                                          \
extern "C" JNIEXPORT jboolean JNICALL                                           \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return JNI_FALSE;                         \
    return cname(handle_as<SpecUtils_Measurement>(h)) ? JNI_TRUE : JNI_FALSE;   \
}

#define MEAS_INT_GETTER(jname, cname)                                           \
extern "C" JNIEXPORT jint JNICALL                                               \
Java_gov_sandia_specutils_internal_Native_##jname                               \
  (JNIEnv *env, jclass, jlong h) {                                              \
    if (check_handle(env, h, #jname)) return 0;                                 \
    return static_cast<jint>(cname(handle_as<SpecUtils_Measurement>(h)));       \
}

MEAS_FLT_GETTER (measurementRealTime,        SpecUtils_Measurement_real_time)
MEAS_FLT_GETTER (measurementLiveTime,        SpecUtils_Measurement_live_time)
MEAS_FLT_GETTER (measurementNeutronLiveTime, SpecUtils_Measurement_neutron_live_time)
MEAS_DBL_GETTER (measurementGammaCountSum,   SpecUtils_Measurement_gamma_count_sum)
MEAS_DBL_GETTER (measurementNeutronCountSum, SpecUtils_Measurement_neutron_count_sum)
MEAS_BOOL_GETTER(measurementIsOccupied,      SpecUtils_Measurement_is_occupied)
MEAS_BOOL_GETTER(measurementContainedNeutron, SpecUtils_Measurement_contained_neutron)
MEAS_INT_GETTER (measurementNumberGammaChannels, SpecUtils_Measurement_number_gamma_channels)
MEAS_INT_GETTER (measurementSampleNumber,    SpecUtils_Measurement_sample_number)
MEAS_FLT_GETTER (measurementSpeed,           SpecUtils_Measurement_speed)
MEAS_BOOL_GETTER(measurementHasGpsInfo,      SpecUtils_Measurement_has_gps_info)
MEAS_DBL_GETTER (measurementLatitude,        SpecUtils_Measurement_latitude)
MEAS_DBL_GETTER (measurementLongitude,       SpecUtils_Measurement_longitude)
MEAS_FLT_GETTER (measurementDoseRate,        SpecUtils_Measurement_dose_rate)
MEAS_FLT_GETTER (measurementExposureRate,    SpecUtils_Measurement_exposure_rate)
MEAS_INT_GETTER (measurementNumberRemarks,   SpecUtils_Measurement_number_remarks)
MEAS_INT_GETTER (measurementNumberParseWarnings, SpecUtils_Measurement_number_parse_warnings)
MEAS_INT_GETTER (measurementOccupancyStatus, SpecUtils_Measurement_occupancy_status)
MEAS_INT_GETTER (measurementQualityStatus,   SpecUtils_Measurement_quality_status)
MEAS_INT_GETTER (measurementSourceType,      SpecUtils_Measurement_source_type)
MEAS_INT_GETTER (measurementDerivedDataProperties, SpecUtils_Measurement_derived_data_properties)

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_measurementStartTimeUsecs
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementStartTimeUsecs")) return 0;
    return SpecUtils_Measurement_start_time_usecs(handle_as<SpecUtils_Measurement>(h));
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_measurementPositionTimeMicrosec
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementPositionTimeMicrosec")) return 0;
    return SpecUtils_Measurement_position_time_microsec(handle_as<SpecUtils_Measurement>(h));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetStartTimeUsecs
  (JNIEnv *env, jclass, jlong h, jlong usecs) {
    if (check_handle(env, h, "measurementSetStartTimeUsecs")) return;
    SpecUtils_Measurement_set_start_time_usecs(handle_as<SpecUtils_Measurement>(h), usecs);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetStartTimeStr
  (JNIEnv *env, jclass, jlong h, jstring jstr) {
    if (check_handle(env, h, "measurementSetStartTimeStr")) return JNI_FALSE;
    JStringUtf8 s(env, jstr);
    return SpecUtils_Measurement_set_start_time_str(
        handle_as<SpecUtils_Measurement>(h), s.c_str()) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jchar JNICALL
Java_gov_sandia_specutils_internal_Native_measurementPcfTag
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementPcfTag")) return 0;
    return (jchar) SpecUtils_Measurement_pcf_tag(handle_as<SpecUtils_Measurement>(h));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetPcfTag
  (JNIEnv *env, jclass, jlong h, jchar c) {
    if (check_handle(env, h, "measurementSetPcfTag")) return;
    SpecUtils_Measurement_set_pcf_tag(handle_as<SpecUtils_Measurement>(h), (char) c);
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetSampleNumber
  (JNIEnv *env, jclass, jlong h, jint n) {
    if (check_handle(env, h, "measurementSetSampleNumber")) return;
    SpecUtils_Measurement_set_sample_number(handle_as<SpecUtils_Measurement>(h), n);
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetOccupancyStatus
  (JNIEnv *env, jclass, jlong h, jint ord) {
    if (check_handle(env, h, "measurementSetOccupancyStatus")) return;
    SpecUtils_Measurement_set_occupancy_status(handle_as<SpecUtils_Measurement>(h),
        static_cast<SpecUtils_OccupancyStatus>(ord));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetSourceType
  (JNIEnv *env, jclass, jlong h, jint ord) {
    if (check_handle(env, h, "measurementSetSourceType")) return;
    SpecUtils_Measurement_set_source_type(handle_as<SpecUtils_Measurement>(h),
        static_cast<SpecUtils_SourceType>(ord));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetPosition
  (JNIEnv *env, jclass, jlong h, jdouble lon, jdouble lat, jlong usecs) {
    if (check_handle(env, h, "measurementSetPosition")) return;
    SpecUtils_Measurement_set_position(handle_as<SpecUtils_Measurement>(h),
        lon, lat, usecs);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_gov_sandia_specutils_internal_Native_measurementGammaChannelCounts
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementGammaChannelCounts")) return nullptr;
    SpecUtils_Measurement *m = handle_as<SpecUtils_Measurement>(h);
    uint32_t n = SpecUtils_Measurement_number_gamma_channels(m);
    const float *data = SpecUtils_Measurement_gamma_channel_counts(m);
    return make_jfloat_array(env, data, data ? n : 0);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_gov_sandia_specutils_internal_Native_measurementEnergyBounds
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementEnergyBounds")) return nullptr;
    SpecUtils_Measurement *m = handle_as<SpecUtils_Measurement>(h);
    uint32_t n = SpecUtils_Measurement_number_gamma_channels(m);
    const float *data = SpecUtils_Measurement_energy_bounds(m);
    return make_jfloat_array(env, data, data ? (n + 1) : 0);
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_measurementEnergyCalibrationPtr
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementEnergyCalibrationPtr")) return 0;
    return to_handle(SpecUtils_Measurement_energy_calibration(
        handle_as<SpecUtils_Measurement>(h)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_measurementEnergyCalibrationRef
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "measurementEnergyCalibrationRef")) return 0;
    return to_handle(SpecUtils_Measurement_energy_calibration_ref(
        handle_as<SpecUtils_Measurement>(h)));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetGammaCounts
  (JNIEnv *env, jclass, jlong h, jfloatArray jcounts, jfloat lt, jfloat rt) {
    if (check_handle(env, h, "measurementSetGammaCounts")) return;
    jsize n = jcounts ? env->GetArrayLength(jcounts) : 0;
    std::vector<jfloat> buf(n);
    if (n > 0) env->GetFloatArrayRegion(jcounts, 0, n, buf.data());
    SpecUtils_Measurement_set_gamma_counts(handle_as<SpecUtils_Measurement>(h),
        buf.data(), static_cast<uint32_t>(n), lt, rt);
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetNeutronCounts
  (JNIEnv *env, jclass, jlong h, jfloatArray jcounts, jfloat nlt) {
    if (check_handle(env, h, "measurementSetNeutronCounts")) return;
    jsize n = jcounts ? env->GetArrayLength(jcounts) : 0;
    std::vector<jfloat> buf(n);
    if (n > 0) env->GetFloatArrayRegion(jcounts, 0, n, buf.data());
    SpecUtils_Measurement_set_neutron_counts(handle_as<SpecUtils_Measurement>(h),
        buf.data(), static_cast<uint32_t>(n), nlt);
}

extern "C" JNIEXPORT jstring JNICALL
Java_gov_sandia_specutils_internal_Native_measurementRemark
  (JNIEnv *env, jclass, jlong h, jint i) {
    if (check_handle(env, h, "measurementRemark")) return nullptr;
    return to_jstring(env, SpecUtils_Measurement_remark(
        handle_as<SpecUtils_Measurement>(h), static_cast<uint32_t>(i)));
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetRemarks
  (JNIEnv *env, jclass, jlong h, jobjectArray jarr) {
    if (check_handle(env, h, "measurementSetRemarks")) return;
    JStringArrayUtf8 arr(env, jarr);
    SpecUtils_Measurement_set_remarks(handle_as<SpecUtils_Measurement>(h),
        const_cast<const char**>(arr.data()), arr.size());
}

extern "C" JNIEXPORT jstring JNICALL
Java_gov_sandia_specutils_internal_Native_measurementParseWarning
  (JNIEnv *env, jclass, jlong h, jint i) {
    if (check_handle(env, h, "measurementParseWarning")) return nullptr;
    return to_jstring(env, SpecUtils_Measurement_parse_warning(
        handle_as<SpecUtils_Measurement>(h), static_cast<uint32_t>(i)));
}

extern "C" JNIEXPORT jdouble JNICALL
Java_gov_sandia_specutils_internal_Native_measurementGammaIntegral
  (JNIEnv *env, jclass, jlong h, jfloat lo, jfloat hi) {
    if (check_handle(env, h, "measurementGammaIntegral")) return 0.0;
    return SpecUtils_Measurement_gamma_integral(handle_as<SpecUtils_Measurement>(h), lo, hi);
}

extern "C" JNIEXPORT jdouble JNICALL
Java_gov_sandia_specutils_internal_Native_measurementGammaChannelsSum
  (JNIEnv *env, jclass, jlong h, jint start, jint end) {
    if (check_handle(env, h, "measurementGammaChannelsSum")) return 0.0;
    return SpecUtils_Measurement_gamma_channels_sum(handle_as<SpecUtils_Measurement>(h),
        static_cast<uint32_t>(start), static_cast<uint32_t>(end));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_measurementCombineGammaChannels
  (JNIEnv *env, jclass, jlong h, jint n) {
    if (check_handle(env, h, "measurementCombineGammaChannels")) return JNI_FALSE;
    return SpecUtils_Measurement_combine_gamma_channels(
        handle_as<SpecUtils_Measurement>(h),
        static_cast<uint32_t>(n)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_measurementRebin
  (JNIEnv *env, jclass, jlong h, jlong refH) {
    if (check_handle(env, h, "measurementRebin")) return JNI_FALSE;
    if (check_handle(env, refH, "measurementRebin cal")) return JNI_FALSE;
    return SpecUtils_Measurement_rebin(handle_as<SpecUtils_Measurement>(h),
        handle_as<SpecUtils_CountedRef_EnergyCal>(refH)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_measurementSetEnergyCalibration
  (JNIEnv *env, jclass, jlong h, jlong refH) {
    if (check_handle(env, h, "measurementSetEnergyCalibration")) return JNI_FALSE;
    if (check_handle(env, refH, "measurementSetEnergyCalibration cal")) return JNI_FALSE;
    return SpecUtils_Measurement_set_energy_calibration(
        handle_as<SpecUtils_Measurement>(h),
        handle_as<SpecUtils_CountedRef_EnergyCal>(refH)) ? JNI_TRUE : JNI_FALSE;
}

// =============================================================================
//  EnergyCalibration
// =============================================================================

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalCreate(JNIEnv *, jclass) {
    return to_handle(SpecUtils_EnergyCal_create());
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalDestroy
  (JNIEnv *, jclass, jlong h) {
    if (h == 0) return;
    SpecUtils_EnergyCal_destroy(handle_as<SpecUtils_EnergyCal>(h));
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_countedRefEnergyCalCreate(JNIEnv *, jclass) {
    return to_handle(SpecUtils_CountedRef_EnergyCal_create());
}

extern "C" JNIEXPORT void JNICALL
Java_gov_sandia_specutils_internal_Native_countedRefEnergyCalDestroy
  (JNIEnv *, jclass, jlong h) {
    if (h == 0) return;
    SpecUtils_CountedRef_EnergyCal_destroy(handle_as<SpecUtils_CountedRef_EnergyCal>(h));
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalPtrFromRef
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalPtrFromRef")) return 0;
    return to_handle(SpecUtils_EnergyCal_ptr_from_ref(
        handle_as<SpecUtils_CountedRef_EnergyCal>(h)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalMakeCountedRef
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalMakeCountedRef")) return 0;
    return to_handle(SpecUtils_EnergyCal_make_counted_ref(
        handle_as<SpecUtils_EnergyCal>(h)));
}

extern "C" JNIEXPORT jint JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalType
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalType")) return 0;
    return static_cast<jint>(SpecUtils_EnergyCal_type(
        handle_as<SpecUtils_EnergyCal>(h)));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalValid
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalValid")) return JNI_FALSE;
    return SpecUtils_EnergyCal_valid(handle_as<SpecUtils_EnergyCal>(h))
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalNumberCoefficients
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalNumberCoefficients")) return 0;
    return static_cast<jint>(SpecUtils_EnergyCal_number_coefficients(
        handle_as<SpecUtils_EnergyCal>(h)));
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalCoefficients
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalCoefficients")) return nullptr;
    auto *cal = handle_as<SpecUtils_EnergyCal>(h);
    uint32_t n = SpecUtils_EnergyCal_number_coefficients(cal);
    const float *data = SpecUtils_EnergyCal_coefficients(cal);
    return make_jfloat_array(env, data, data ? n : 0);
}

extern "C" JNIEXPORT jint JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalNumberDeviationPairs
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalNumberDeviationPairs")) return 0;
    return static_cast<jint>(SpecUtils_EnergyCal_number_deviation_pairs(
        handle_as<SpecUtils_EnergyCal>(h)));
}

extern "C" JNIEXPORT jfloat JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalDeviationEnergy
  (JNIEnv *env, jclass, jlong h, jint i) {
    if (check_handle(env, h, "energyCalDeviationEnergy")) return 0.0f;
    return SpecUtils_EnergyCal_deviation_energy(
        handle_as<SpecUtils_EnergyCal>(h), static_cast<uint32_t>(i));
}

extern "C" JNIEXPORT jfloat JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalDeviationOffset
  (JNIEnv *env, jclass, jlong h, jint i) {
    if (check_handle(env, h, "energyCalDeviationOffset")) return 0.0f;
    return SpecUtils_EnergyCal_deviation_offset(
        handle_as<SpecUtils_EnergyCal>(h), static_cast<uint32_t>(i));
}

extern "C" JNIEXPORT jint JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalNumberChannels
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalNumberChannels")) return 0;
    return static_cast<jint>(SpecUtils_EnergyCal_number_channels(
        handle_as<SpecUtils_EnergyCal>(h)));
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalChannelEnergies
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalChannelEnergies")) return nullptr;
    auto *cal = handle_as<SpecUtils_EnergyCal>(h);
    uint32_t n = SpecUtils_EnergyCal_number_channels(cal);
    const float *data = SpecUtils_EnergyCal_channel_energies(cal);
    // "one more entry than number of channels"
    return make_jfloat_array(env, data, data ? (n + 1) : 0);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalSetPolynomial
  (JNIEnv *env, jclass, jlong h, jint numChannels,
   jfloatArray jcoeffs, jfloatArray jdev) {
    if (check_handle(env, h, "energyCalSetPolynomial")) return JNI_FALSE;
    jsize nc = jcoeffs ? env->GetArrayLength(jcoeffs) : 0;
    jsize nd = jdev ? env->GetArrayLength(jdev) : 0;
    std::vector<jfloat> cbuf(nc), dbuf(nd);
    if (nc > 0) env->GetFloatArrayRegion(jcoeffs, 0, nc, cbuf.data());
    if (nd > 0) env->GetFloatArrayRegion(jdev, 0, nd, dbuf.data());
    return SpecUtils_EnergyCal_set_polynomial(
        handle_as<SpecUtils_EnergyCal>(h),
        static_cast<uint32_t>(numChannels),
        cbuf.data(), static_cast<uint32_t>(nc),
        nd > 0 ? dbuf.data() : nullptr, static_cast<uint32_t>(nd / 2))
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalSetFullRangeFraction
  (JNIEnv *env, jclass, jlong h, jint numChannels,
   jfloatArray jcoeffs, jfloatArray jdev) {
    if (check_handle(env, h, "energyCalSetFullRangeFraction")) return JNI_FALSE;
    jsize nc = jcoeffs ? env->GetArrayLength(jcoeffs) : 0;
    jsize nd = jdev ? env->GetArrayLength(jdev) : 0;
    std::vector<jfloat> cbuf(nc), dbuf(nd);
    if (nc > 0) env->GetFloatArrayRegion(jcoeffs, 0, nc, cbuf.data());
    if (nd > 0) env->GetFloatArrayRegion(jdev, 0, nd, dbuf.data());
    return SpecUtils_EnergyCal_set_full_range_fraction(
        handle_as<SpecUtils_EnergyCal>(h),
        static_cast<uint32_t>(numChannels),
        cbuf.data(), static_cast<uint32_t>(nc),
        nd > 0 ? dbuf.data() : nullptr, static_cast<uint32_t>(nd / 2))
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalSetLowerChannelEnergy
  (JNIEnv *env, jclass, jlong h, jint numChannels, jfloatArray jenergies) {
    if (check_handle(env, h, "energyCalSetLowerChannelEnergy")) return JNI_FALSE;
    jsize ne = jenergies ? env->GetArrayLength(jenergies) : 0;
    std::vector<jfloat> buf(ne);
    if (ne > 0) env->GetFloatArrayRegion(jenergies, 0, ne, buf.data());
    return SpecUtils_EnergyCal_set_lower_channel_energy(
        handle_as<SpecUtils_EnergyCal>(h),
        static_cast<uint32_t>(numChannels),
        static_cast<uint32_t>(ne),
        buf.data()) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jdouble JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalChannelForEnergy
  (JNIEnv *env, jclass, jlong h, jdouble energy) {
    if (check_handle(env, h, "energyCalChannelForEnergy")) return 0.0;
    return SpecUtils_EnergyCal_channel_for_energy(
        handle_as<SpecUtils_EnergyCal>(h), energy);
}

extern "C" JNIEXPORT jdouble JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalEnergyForChannel
  (JNIEnv *env, jclass, jlong h, jdouble channel) {
    if (check_handle(env, h, "energyCalEnergyForChannel")) return 0.0;
    return SpecUtils_EnergyCal_energy_for_channel(
        handle_as<SpecUtils_EnergyCal>(h), channel);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalLowerEnergy
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalLowerEnergy")) return 0.0f;
    return SpecUtils_EnergyCal_lower_energy(handle_as<SpecUtils_EnergyCal>(h));
}

extern "C" JNIEXPORT jfloat JNICALL
Java_gov_sandia_specutils_internal_Native_energyCalUpperEnergy
  (JNIEnv *env, jclass, jlong h) {
    if (check_handle(env, h, "energyCalUpperEnergy")) return 0.0f;
    return SpecUtils_EnergyCal_upper_energy(handle_as<SpecUtils_EnergyCal>(h));
}
