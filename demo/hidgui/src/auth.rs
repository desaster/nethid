// HTTP auth check + token fetch

use serde::Deserialize;

#[derive(Deserialize)]
struct AuthStatus {
    required: bool,
}

#[derive(Deserialize)]
struct LoginResponse {
    token: String,
}

// Returns None if auth not required (use v1), Some(token) if authenticated (use v2).
// Exits on auth failure.
pub fn authenticate(host: &str, password: Option<&str>) -> Option<[u8; 16]> {
    let base = format!("http://{host}");

    let status: AuthStatus = match ureq::get(&format!("{base}/api/auth/status")).call() {
        Ok(mut resp) => resp.body_mut().read_json().unwrap_or_else(|e| {
            eprintln!("Error parsing auth status: {e}");
            std::process::exit(1);
        }),
        Err(e) => {
            eprintln!("Error checking auth status: {e}");
            std::process::exit(1);
        }
    };

    if !status.required {
        return None;
    }

    let Some(password) = password else {
        eprintln!("Error: device requires authentication. Use --password or set NETHID_PASSWORD.");
        std::process::exit(1);
    };

    let body = serde_json::json!({"password": password});

    let login: LoginResponse = match ureq::post(&format!("{base}/api/login")).send_json(&body) {
        Ok(mut resp) => resp.body_mut().read_json().unwrap_or_else(|e| {
            eprintln!("Error parsing login response: {e}");
            std::process::exit(1);
        }),
        Err(e) => {
            eprintln!("Error: login failed: {e}");
            std::process::exit(1);
        }
    };

    let token: [u8; 16] = hex::decode(&login.token)
        .ok()
        .and_then(|v| v.try_into().ok())
        .unwrap_or_else(|| {
            eprintln!("Error: bad token from server");
            std::process::exit(1);
        });

    Some(token)
}
