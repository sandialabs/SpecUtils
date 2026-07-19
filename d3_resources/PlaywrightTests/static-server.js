// Minimal dependency-free static file server, rooted at the d3_resources directory
// (the parent of this folder) so the harness can load ../../SpectrumChartD3.js,
// ../../d3.v3.min.js, ../../SpectrumChartD3.css and ../../example_json_with_peaks.json.
//
// Used by playwright.config.js `webServer`. Run standalone with:  node static-server.js [port]

const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');          // d3_resources/
const PORT = Number(process.argv[2] || process.env.PORT || 8125);

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'text/javascript; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg':  'image/svg+xml',
  '.png':  'image/png',
};

const server = http.createServer( function( req, res ){
  // Strip query string, decode, and resolve safely inside ROOT (no path traversal).
  const urlPath = decodeURIComponent( req.url.split('?')[0] );
  const rel = path.normalize( urlPath ).replace( /^(\.\.[\/\\])+/, '' );
  const full = path.join( ROOT, rel );

  if( !full.startsWith( ROOT ) ){
    res.writeHead( 403 ); res.end( 'Forbidden' ); return;
  }

  fs.stat( full, function( err, st ){
    if( err || !st.isFile() ){
      res.writeHead( 404 ); res.end( 'Not found: ' + rel ); return;
    }
    res.writeHead( 200, { 'Content-Type': MIME[path.extname(full)] || 'application/octet-stream' } );
    fs.createReadStream( full ).pipe( res );
  });
});

server.listen( PORT, '127.0.0.1', function(){
  console.log( 'SpectrumChartD3 test server: http://127.0.0.1:' + PORT + '  (root: ' + ROOT + ')' );
});
