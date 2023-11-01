wit_bindgen::generate!({
    world: "wasi:http/proxy",
    exports: {
        "wasi:http/incoming-handler": Component,
    },
});

use crate::exports::wasi::http::incoming_handler;
use crate::wasi::http::types;
use crate::wasi::io::poll;
use crate::wasi::io::streams::StreamError;
use std::fmt::Write;

struct Component;

macro_rules! uwriteln {
    ($($fmt:tt)*) => (writeln!($($fmt)*).unwrap());
}

impl incoming_handler::Guest for Component {
    fn handle(request: types::IncomingRequest, response_outparam: types::ResponseOutparam) {
        let mut content = String::new();
        uwriteln!(content, " * Welcome to the component model in Rust! *\n");
        uwriteln!(content, "[Request Info]");
        if let Some(path) = request.path_with_query() {
            uwriteln!(content, "REQUEST_PATH = {path}");
        }
        uwriteln!(content, "METHOD = {}", method_to_str(&request.method()));
        if let Some(scheme) = request.scheme() {
            uwriteln!(content, "SCHEME = {}", scheme_to_str(&scheme));
        }
        if let Some(name) = request.authority() {
            uwriteln!(content, "AUTHORITY = {name}");
        }
        uwriteln!(content, "\n[Request Headers]");
        for (key, value) in request.headers().entries() {
            uwriteln!(content, "{key} = {}", String::from_utf8_lossy(&value));
        }

        let body = request.consume().expect("failed to consume request");
        let reader = body.stream().expect("failed to get a reading stream");
        let mut any = false;
        loop {
            match reader.read(64 * 1024) {
                Ok(list) => {
                    if list.is_empty() {
                        poll::poll_one(&reader.subscribe());
                        continue;
                    }
                    if !any {
                        uwriteln!(content, "\n[Request Data]");
                        any = true;
                    }
                    content.push_str(&String::from_utf8_lossy(&list));
                }
                Err(StreamError::LastOperationFailed(_)) => panic!("read failed"),
                Err(StreamError::Closed) => break,
            }
        }

        // Generate the response.
        let headers = types::Headers::new(&[]);
        headers.append("content-type", b"text/plain");
        headers.append("content-length", format!("{}", content.len()).as_bytes());
        let response = types::OutgoingResponse::new(200, &headers);
        let outgoing_body = response.write().expect("failed to get write stream");

        // Send the headers.
        types::ResponseOutparam::set(response_outparam, Ok(response));

        // Write the body.
        outgoing_body
            .write()
            .expect("failed to get write stream")
            .blocking_write_and_flush(content.as_bytes())
            .expect("failed to write");
        types::OutgoingBody::finish(outgoing_body, None);
    }
}

fn method_to_str(method: &types::Method) -> &str {
    match method {
        types::Method::Get => "GET",
        types::Method::Head => "HEAD",
        types::Method::Post => "POST",
        types::Method::Put => "PUT",
        types::Method::Delete => "DELETEE",
        types::Method::Connect => "CONNECT",
        types::Method::Options => "OPTIONS",
        types::Method::Trace => "TRACE",
        types::Method::Patch => "PATCH",
        types::Method::Other(s) => s,
    }
}

fn scheme_to_str(scheme: &types::Scheme) -> &str {
    match scheme {
        types::Scheme::Http => "http",
        types::Scheme::Https => "https",
        types::Scheme::Other(s) => s,
    }
}
