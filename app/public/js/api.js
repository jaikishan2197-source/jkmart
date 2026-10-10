/* Sentry-style API layer for the JK Mart backend (Drogon + libpq JSON APIs). */
const API = {
  base: "",

  token() {
    return localStorage.getItem("jkmart_token") || "";
  },

  async request(method, path, body, auth) {
    const headers = {};
    if (body !== undefined && body !== null) {
      headers["Content-Type"] = "application/json";
    }
    if (auth) {
      const token = this.token();
      if (token) {
        headers["Authorization"] = "Bearer " + token;
      }
    }
    let res;
    try {
      res = await fetch(this.base + path, {
        method,
        headers,
        body: body === undefined || body === null ? undefined : JSON.stringify(body),
      });
    } catch (e) {
      throw new Error("Network error - is the server running?");
    }
    let data = null;
    try {
      data = await res.json();
    } catch (e) {
      /* empty body */
    }
    if (!res.ok) {
      const err = new Error((data && data.error) || "Request failed (" + res.status + ")");
      err.status = res.status;
      err.data = data;
      throw err;
    }
    return data;
  },

  get(path, auth) {
    return this.request("GET", path, null, auth);
  },
  post(path, body, auth) {
    return this.request("POST", path, body, auth);
  },
  put(path, body, auth) {
    return this.request("PUT", path, body, auth);
  },
  del(path, auth) {
    return this.request("DELETE", path, null, auth);
  },
};

const Auth = {
  set(data) {
    localStorage.setItem("jkmart_token", data.token || "");
    localStorage.setItem("jkmart_user", JSON.stringify(data.user || {}));
  },
  user() {
    try {
      return JSON.parse(localStorage.getItem("jkmart_user")) || null;
    } catch (e) {
      return null;
    }
  },
  clear() {
    localStorage.removeItem("jkmart_token");
    localStorage.removeItem("jkmart_user");
  },
  isLoggedIn() {
    return !!localStorage.getItem("jkmart_token");
  },
  async logout() {
    try {
      if (Auth.isLoggedIn()) {
        await API.post("/api/auth/logout", null, true);
      }
    } catch (e) {
      /* ignore session issues on logout */
    }
    Auth.clear();
  },
};