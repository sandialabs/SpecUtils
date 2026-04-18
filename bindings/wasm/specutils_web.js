/* SpecUtils WebAssembly JavaScript API
 *
 * Provides a clean JavaScript API wrapping the SpecUtils C bindings compiled
 * to WebAssembly via Emscripten. Used by both the full viewer and minimal example.
 *
 * Usage:
 *   const Module = await SpecUtilsModule();
 *   const api = new SpecUtilsAPI(Module);
 *   const info = api.loadFile(uint8Array, 'spectrum.n42');
 *   const chartData = api.getChartData(info.samples, info.detectors);
 *   const exportedBytes = api.exportFile(4); // N42-2012 format index
 *   api.destroy();
 *
 * Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
 * LGPL v2.1 License.
 */

// Export format definitions matching SpecUtils::SaveSpectrumAsType enum
var EXPORT_FORMATS = [
  { name: 'TXT',                index: 0 },
  { name: 'CSV',                index: 1 },
  { name: 'PCF',                index: 2 },
  { name: 'N42-2006',           index: 3 },
  { name: 'N42-2012',           index: 4 },
  { name: 'CHN (ORTEC)',        index: 5 },
  { name: 'SPC (Binary Int)',   index: 6 },
  { name: 'SPC (Binary Float)', index: 7 },
  { name: 'SPC (ASCII)',        index: 8 },
  { name: 'GR-130 (v0)',        index: 9 },
  { name: 'GR-135 (v2)',        index: 10 },
  { name: 'SPE (IAEA)',         index: 11 },
  { name: 'CNF (Canberra)',     index: 12 },
  { name: 'TKA',                index: 13 }
];

// File extension suggestions for each format
var EXPORT_EXTENSIONS = {
  0: '.txt', 1: '.csv', 2: '.pcf', 3: '.n42', 4: '.n42',
  5: '.chn', 6: '.spc', 7: '.spc', 8: '.spc', 9: '.gr130',
  10: '.gr135', 11: '.spe', 12: '.cnf', 13: '.tka'
};

// Spectrum line colors for multi-detector display
var SPECTRUM_COLORS = [
  'black', 'steelblue', 'red', 'green', 'purple',
  'orange', 'brown', 'magenta', 'olive', 'teal'
];

/**
 * SpecUtilsAPI - High-level JavaScript API for SpecUtils WASM module.
 * @param {Object} Module - The initialized Emscripten Module
 */
function SpecUtilsAPI(Module) {
  this._Module = Module;
  this._specFilePtr = null;

  // Set up cwrap bindings to C functions
  this._cwrap = {};
  this._initCwrap();
}

SpecUtilsAPI.prototype._initCwrap = function() {
  var M = this._Module;

  // SpecFile lifecycle
  this._cwrap.createSpecFile = M.cwrap('SpecUtils_SpecFile_create', 'number', []);
  this._cwrap.destroySpecFile = M.cwrap('SpecUtils_SpecFile_destroy', null, ['number']);

  // Loading
  this._cwrap.loadFile = M.cwrap('SpecFile_load_file', 'number', ['number', 'string']);
  this._cwrap.loadFromBuffer = M.cwrap('SpecUtils_load_from_buffer', 'number',
    ['number', 'number', 'number', 'string']);

  // Export
  this._cwrap.writeToFile = M.cwrap('SpecUtils_write_to_file', 'number',
    ['number', 'string', 'number']);
  this._cwrap.exportToBuffer = M.cwrap('SpecUtils_export_to_buffer', 'number',
    ['number', 'number', 'number']);
  this._cwrap.freeExportBuffer = M.cwrap('SpecUtils_free_export_buffer', null, ['number']);

  // SpecFile info
  this._cwrap.numMeasurements = M.cwrap('SpecUtils_SpecFile_number_measurements', 'number', ['number']);
  this._cwrap.numGammaChannels = M.cwrap('SpecUtils_SpecFile_number_gamma_channels', 'number', ['number']);
  this._cwrap.passthrough = M.cwrap('SpecUtils_SpecFile_passthrough', 'number', ['number']);
  this._cwrap.filename = M.cwrap('SpecUtils_SpecFile_filename', 'string', ['number']);
  this._cwrap.uuid = M.cwrap('SpecUtils_SpecFile_uuid', 'string', ['number']);
  this._cwrap.manufacturer = M.cwrap('SpecUtils_SpecFile_manufacturer', 'string', ['number']);
  this._cwrap.instrumentModel = M.cwrap('SpecUtils_SpecFile_instrument_model', 'string', ['number']);
  this._cwrap.instrumentType = M.cwrap('SpecUtils_SpecFile_instrument_type', 'string', ['number']);

  this._cwrap.sumGammaLiveTime = M.cwrap('SpecUtils_SpecFile_sum_gamma_live_time', 'number', ['number']);
  this._cwrap.sumGammaRealTime = M.cwrap('SpecUtils_SpecFile_sum_gamma_real_time', 'number', ['number']);
  this._cwrap.gammaCountSum = M.cwrap('SpecUtils_SpecFile_gamma_count_sum', 'number', ['number']);
  this._cwrap.neutronCountsSum = M.cwrap('SpecUtils_SpecFile_neutron_counts_sum', 'number', ['number']);

  // Samples
  this._cwrap.numSamples = M.cwrap('SpecUtils_SpecFile_number_samples', 'number', ['number']);
  this._cwrap.sampleNumber = M.cwrap('SpecUtils_SpecFile_sample_number', 'number', ['number', 'number']);

  // Detectors
  this._cwrap.numDetectors = M.cwrap('SpecUtils_SpecFile_number_detectors', 'number', ['number']);
  this._cwrap.detectorName = M.cwrap('SpecUtils_SpecFile_detector_name', 'string', ['number', 'number']);
  this._cwrap.numGammaDetectors = M.cwrap('SpecUtils_SpecFile_number_gamma_detectors', 'number', ['number']);
  this._cwrap.gammaDetectorName = M.cwrap('SpecUtils_SpecFile_gamma_detector_name', 'string', ['number', 'number']);
  this._cwrap.numNeutronDetectors = M.cwrap('SpecUtils_SpecFile_number_neutron_detectors', 'number', ['number']);

  // Remarks
  this._cwrap.numRemarks = M.cwrap('SpecUtils_SpecFile_number_remarks', 'number', ['number']);
  this._cwrap.remark = M.cwrap('SpecUtils_SpecFile_remark', 'string', ['number', 'number']);

  // Sum measurements
  this._cwrap.sumMeasurements = M.cwrap('SpecUtils_SpecFile_sum_measurements', 'number',
    ['number', 'number', 'number', 'number', 'number']);

  // Individual measurement access
  this._cwrap.getMeasByIndex = M.cwrap('SpecUtils_SpecFile_get_measurement_by_index', 'number',
    ['number', 'number']);
  this._cwrap.getMeasBySampleDet = M.cwrap('SpecUtils_SpecFile_get_measurement_by_sample_det', 'number',
    ['number', 'number', 'string']);

  // Measurement data
  this._cwrap.measNumChannels = M.cwrap('SpecUtils_Measurement_number_gamma_channels', 'number', ['number']);
  this._cwrap.measChannelCounts = M.cwrap('SpecUtils_Measurement_gamma_channel_counts', 'number', ['number']);
  this._cwrap.measEnergyBounds = M.cwrap('SpecUtils_Measurement_energy_bounds', 'number', ['number']);
  this._cwrap.measLiveTime = M.cwrap('SpecUtils_Measurement_live_time', 'number', ['number']);
  this._cwrap.measRealTime = M.cwrap('SpecUtils_Measurement_real_time', 'number', ['number']);
  this._cwrap.measGammaSum = M.cwrap('SpecUtils_Measurement_gamma_count_sum', 'number', ['number']);
  this._cwrap.measNeutronSum = M.cwrap('SpecUtils_Measurement_neutron_count_sum', 'number', ['number']);
  this._cwrap.measContainedNeutron = M.cwrap('SpecUtils_Measurement_contained_neutron', 'number', ['number']);
  this._cwrap.measSampleNumber = M.cwrap('SpecUtils_Measurement_sample_number', 'number', ['number']);
  this._cwrap.measDetectorName = M.cwrap('SpecUtils_Measurement_detector_name', 'string', ['number']);
  this._cwrap.measTitle = M.cwrap('SpecUtils_Measurement_title', 'string', ['number']);
  this._cwrap.measStartTimeUsecs = M.cwrap('SpecUtils_Measurement_start_time_usecs', 'number', ['number']);

  // Measurement lifecycle (for sum results we own)
  this._cwrap.destroyMeasurement = M.cwrap('SpecUtils_Measurement_destroy', null, ['number']);
};


/**
 * Load a spectrum file from a Uint8Array.
 * @param {Uint8Array} fileBytes - The raw file bytes
 * @param {string} filename - Original filename (used for format detection)
 * @returns {Object} File info: {samples, detectors, gammaDetectors, metadata, ...}
 */
SpecUtilsAPI.prototype.loadFile = function(fileBytes, filename) {
  // Clean up any previous file
  if (this._specFilePtr) {
    this._cwrap.destroySpecFile(this._specFilePtr);
    this._specFilePtr = null;
  }

  this._specFilePtr = this._cwrap.createSpecFile();
  if (!this._specFilePtr)
    throw new Error('Failed to create SpecFile');

  // Write file to Emscripten's virtual filesystem (MEMFS), then load by path.
  // This avoids WASM heap issues with ALLOW_MEMORY_GROWTH (detached ArrayBuffer).
  var M = this._Module;
  var tmpPath = '/tmp/_specutils_input_' + Date.now();
  M.FS.writeFile(tmpPath, fileBytes);

  var ok = this._cwrap.loadFile(this._specFilePtr, tmpPath);

  try { M.FS.unlink(tmpPath); } catch(e) { /* ignore cleanup errors */ }

  if (!ok) {
    this._cwrap.destroySpecFile(this._specFilePtr);
    this._specFilePtr = null;
    throw new Error('Failed to parse spectrum file: ' + filename);
  }

  // Remember the original filename (the C API returns the MEMFS temp path)
  this._originalFilename = filename || '';

  return this.getFileInfo();
};


/**
 * Get information about the currently loaded file.
 * @returns {Object} File metadata and structure
 */
SpecUtilsAPI.prototype.getFileInfo = function() {
  var ptr = this._specFilePtr;
  if (!ptr) return null;

  var samples = [];
  var numSamples = this._cwrap.numSamples(ptr);
  for (var i = 0; i < numSamples; i++) {
    samples.push(this._cwrap.sampleNumber(ptr, i));
  }

  var detectors = [];
  var numDets = this._cwrap.numDetectors(ptr);
  for (var i = 0; i < numDets; i++) {
    detectors.push(this._cwrap.detectorName(ptr, i));
  }

  var gammaDetectors = [];
  var numGammaDets = this._cwrap.numGammaDetectors(ptr);
  for (var i = 0; i < numGammaDets; i++) {
    gammaDetectors.push(this._cwrap.gammaDetectorName(ptr, i));
  }

  var remarks = [];
  var numRemarks = this._cwrap.numRemarks(ptr);
  for (var i = 0; i < numRemarks; i++) {
    remarks.push(this._cwrap.remark(ptr, i));
  }

  return {
    samples: samples,
    detectors: detectors,
    gammaDetectors: gammaDetectors,
    numMeasurements: this._cwrap.numMeasurements(ptr),
    maxGammaChannels: this._cwrap.numGammaChannels(ptr),
    passthrough: this._cwrap.passthrough(ptr),
    sumGammaLiveTime: this._cwrap.sumGammaLiveTime(ptr),
    sumGammaRealTime: this._cwrap.sumGammaRealTime(ptr),
    gammaCountSum: this._cwrap.gammaCountSum(ptr),
    neutronCountsSum: this._cwrap.neutronCountsSum(ptr),
    numNeutronDetectors: this._cwrap.numNeutronDetectors(ptr),
    remarks: remarks,
    metadata: {
      filename: this._originalFilename || this._cwrap.filename(ptr),
      uuid: this._cwrap.uuid(ptr),
      manufacturer: this._cwrap.manufacturer(ptr),
      instrumentModel: this._cwrap.instrumentModel(ptr),
      instrumentType: this._cwrap.instrumentType(ptr)
    }
  };
};


/**
 * Get information about a specific measurement (sample + detector).
 * The returned measurement pointer is NOT owned by the caller.
 * @param {number} sampleNumber
 * @param {string} detectorName
 * @returns {Object|null} Measurement info
 */
SpecUtilsAPI.prototype.getMeasurementInfo = function(sampleNumber, detectorName) {
  var ptr = this._specFilePtr;
  if (!ptr) return null;

  var measPtr = this._cwrap.getMeasBySampleDet(ptr, sampleNumber, detectorName);
  if (!measPtr) return null;

  return this._extractMeasurementInfo(measPtr);
};


/**
 * Extract info from a measurement pointer (does NOT destroy the measurement).
 */
SpecUtilsAPI.prototype._extractMeasurementInfo = function(measPtr) {
  return {
    numChannels: this._cwrap.measNumChannels(measPtr),
    liveTime: this._cwrap.measLiveTime(measPtr),
    realTime: this._cwrap.measRealTime(measPtr),
    gammaCountSum: this._cwrap.measGammaSum(measPtr),
    neutronCountSum: this._cwrap.measNeutronSum(measPtr),
    containedNeutron: this._cwrap.measContainedNeutron(measPtr),
    sampleNumber: this._cwrap.measSampleNumber(measPtr),
    detectorName: this._cwrap.measDetectorName(measPtr),
    title: this._cwrap.measTitle(measPtr)
  };
};


/**
 * Extract spectrum x/y data from a measurement pointer.
 * Returns data in the format expected by SpectrumChartD3.
 */
SpecUtilsAPI.prototype._extractSpectrumData = function(measPtr, title, lineColor) {
  var M = this._Module;

  var nChannels = this._cwrap.measNumChannels(measPtr);
  if (nChannels === 0) return null;

  var countsPtr = this._cwrap.measChannelCounts(measPtr);
  var energyPtr = this._cwrap.measEnergyBounds(measPtr);
  if (!countsPtr || !energyPtr) return null;

  // Read float arrays from WASM heap (Float32).
  // Use M.HEAPF32 directly (Emscripten updates it when memory grows).
  // Copy to regular JS arrays immediately since pointers are into WASM memory.
  var x = new Array(nChannels + 1);
  var y = new Array(nChannels);
  for (var i = 0; i <= nChannels; i++) {
    x[i] = M.HEAPF32[(energyPtr >> 2) + i];
  }
  for (var i = 0; i < nChannels; i++) {
    y[i] = M.HEAPF32[(countsPtr >> 2) + i];
  }

  var neutronSum = this._cwrap.measNeutronSum(measPtr);
  var containedNeutron = this._cwrap.measContainedNeutron(measPtr);

  return {
    title: title || '',
    peaks: [],
    liveTime: this._cwrap.measLiveTime(measPtr),
    realTime: this._cwrap.measRealTime(measPtr),
    neutrons: containedNeutron ? neutronSum : null,
    lineColor: lineColor || 'black',
    x: x,
    y: y,
    yScaleFactor: 1.0
  };
};


/**
 * Allocate a string in WASM memory. Caller must free the returned pointer.
 */
SpecUtilsAPI.prototype._allocString = function(str) {
  var M = this._Module;
  var len = M.lengthBytesUTF8(str) + 1;
  var ptr = M._malloc(len);
  M.stringToUTF8(str, ptr, len);
  return ptr;
};


/**
 * Call sum_measurements with the given sample numbers and detector names.
 * Returns an OWNED measurement pointer that the caller must destroy.
 */
SpecUtilsAPI.prototype._sumMeasurements = function(sampleNumbers, detectorNames) {
  var M = this._Module;
  var ptr = this._specFilePtr;

  // Allocate all memory first, then write values.
  // After _malloc with ALLOW_MEMORY_GROWTH, HEAP views may be stale,
  // so always use M.HEAP32[offset >> 2] which accesses the current buffer.

  // Allocate sample numbers array (int32)
  var samplesPtr = M._malloc(sampleNumbers.length * 4);

  // Allocate detector name strings
  var stringPtrs = [];
  for (var i = 0; i < detectorNames.length; i++) {
    stringPtrs.push(this._allocString(detectorNames[i]));
  }

  // Allocate array of string pointers
  var detNamesArrayPtr = M._malloc(detectorNames.length * 4);

  // Now write values — HEAP32 is current after all mallocs are done
  for (var i = 0; i < sampleNumbers.length; i++) {
    M.HEAP32[(samplesPtr >> 2) + i] = sampleNumbers[i];
  }
  for (var i = 0; i < detectorNames.length; i++) {
    M.HEAP32[(detNamesArrayPtr >> 2) + i] = stringPtrs[i];
  }

  // Call sum_measurements
  var measPtr = this._cwrap.sumMeasurements(
    ptr, samplesPtr, sampleNumbers.length, detNamesArrayPtr, detectorNames.length
  );

  // Free allocated memory
  M._free(samplesPtr);
  for (var i = 0; i < stringPtrs.length; i++) {
    M._free(stringPtrs[i]);
  }
  M._free(detNamesArrayPtr);

  return measPtr; // caller must destroy with _cwrap.destroyMeasurement
};


/**
 * Get spectrum data formatted for SpectrumChartD3.
 *
 * All selected detectors are summed together into a single spectrum.
 *
 * @param {number[]} sampleNumbers - Sample numbers to sum
 * @param {string[]} detectorNames - Detector names to sum together
 * @returns {Object} Chart data: { spectra: [{title, x, y, liveTime, ...}] }
 */
SpecUtilsAPI.prototype.getChartData = function(sampleNumbers, detectorNames) {
  if (!this._specFilePtr || !sampleNumbers.length || !detectorNames.length) {
    return { spectra: [] };
  }

  // Sum all selected samples and detectors into one measurement
  var measPtr = this._sumMeasurements(sampleNumbers, detectorNames);
  if (!measPtr) return { spectra: [] };

  var spectrum = this._extractSpectrumData(measPtr, '', 'black');
  this._cwrap.destroyMeasurement(measPtr);

  if (!spectrum) return { spectra: [] };

  return { spectra: [spectrum] };
};


/**
 * Export the currently loaded file in the specified format.
 * @param {number} formatIndex - Index from EXPORT_FORMATS
 * @returns {Uint8Array} The exported file bytes
 */
SpecUtilsAPI.prototype.exportFile = function(formatIndex) {
  if (!this._specFilePtr)
    throw new Error('No file loaded');

  var M = this._Module;

  // Use MEMFS-based export to avoid WASM heap buffer issues
  var tmpPath = '/tmp/_specutils_export_' + Date.now();
  var ok = this._cwrap.writeToFile(this._specFilePtr, tmpPath, formatIndex);

  if (!ok) {
    try { M.FS.unlink(tmpPath); } catch(e) {}
    throw new Error('Export failed for format index ' + formatIndex);
  }

  var result = M.FS.readFile(tmpPath, { encoding: 'binary' });
  try { M.FS.unlink(tmpPath); } catch(e) {}

  return result;
};


/**
 * Get available export formats.
 * @returns {Array<{name: string, index: number}>}
 */
SpecUtilsAPI.prototype.getExportFormats = function() {
  return EXPORT_FORMATS;
};


/**
 * Clean up WASM resources.
 */
SpecUtilsAPI.prototype.destroy = function() {
  if (this._specFilePtr) {
    this._cwrap.destroySpecFile(this._specFilePtr);
    this._specFilePtr = null;
  }
};


/**
 * Parse a sample range string like "1-3,5-12,15" into an array of integers.
 * Returns sorted unique values. Ignores invalid tokens.
 * @param {string} rangeStr
 * @param {number[]} validSamples - array of valid sample numbers to clamp to
 * @returns {number[]}
 */
function parseSampleRange(rangeStr, validSamples) {
  if (!rangeStr || !rangeStr.trim()) return [];

  var validSet = {};
  for (var i = 0; i < validSamples.length; i++) validSet[validSamples[i]] = true;

  var result = {};
  var parts = rangeStr.split(',');
  for (var p = 0; p < parts.length; p++) {
    var part = parts[p].trim();
    if (!part) continue;

    var dashIdx = part.indexOf('-', part.charAt(0) === '-' ? 1 : 0);
    if (dashIdx > 0) {
      var lo = parseInt(part.substring(0, dashIdx));
      var hi = parseInt(part.substring(dashIdx + 1));
      if (isNaN(lo) || isNaN(hi)) continue;
      if (lo > hi) { var tmp = lo; lo = hi; hi = tmp; }
      // Cap range to prevent huge loops
      if (hi - lo > 10000) hi = lo + 10000;
      for (var n = lo; n <= hi; n++) {
        if (validSet[n]) result[n] = true;
      }
    } else {
      var num = parseInt(part);
      if (!isNaN(num) && validSet[num]) result[num] = true;
    }
  }

  var arr = [];
  for (var k in result) arr.push(parseInt(k));
  arr.sort(function(a, b) { return a - b; });
  return arr;
}


/**
 * Format a compact description of which samples are in a range string result.
 * e.g., [1,2,3,5,6,15] => "1-3, 5-6, 15"
 * @param {number[]} samples - sorted array
 * @returns {string}
 */
function formatSampleRange(samples) {
  if (!samples.length) return '';
  var parts = [];
  var start = samples[0], end = samples[0];
  for (var i = 1; i < samples.length; i++) {
    if (samples[i] === end + 1) {
      end = samples[i];
    } else {
      parts.push(start === end ? '' + start : start + '-' + end);
      start = end = samples[i];
    }
  }
  parts.push(start === end ? '' + start : start + '-' + end);
  return parts.join(', ');
}


/**
 * Trigger a browser download of a Uint8Array as a file.
 * @param {Uint8Array} data
 * @param {string} filename
 * @param {string} [mimeType='application/octet-stream']
 */
function downloadFile(data, filename, mimeType) {
  var blob = new Blob([data], { type: mimeType || 'application/octet-stream' });
  var a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(a.href);
}


/**
 * Get the suggested file extension for an export format index.
 * @param {number} formatIndex
 * @returns {string}
 */
function getExportExtension(formatIndex) {
  return EXPORT_EXTENSIONS[formatIndex] || '.dat';
}


/**
 * Replace the file extension in a filename.
 * @param {string} filename
 * @param {string} newExt - e.g. '.n42'
 * @returns {string}
 */
function replaceExtension(filename, newExt) {
  var dotIdx = filename.lastIndexOf('.');
  if (dotIdx > 0) {
    return filename.substring(0, dotIdx) + newExt;
  }
  return filename + newExt;
}
