/* Shared UI helpers for the JK Mart storefront. */

function esc(s) {
  return String(s == null ? "" : s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function money(v) {
  const n = parseFloat(v);
  if (isNaN(n)) return "₹0";
  const max = n % 1 === 0 ? 0 : 2;
  return "₹" + n.toLocaleString("en-IN", { maximumFractionDigits: max });
}

function starsHtml(rating, count) {
  const r = parseFloat(rating) || 0;
  if (r <= 0 || (count !== undefined && parseInt(count, 10) <= 0)) {
    return '<span class="meta-badge badge blue">New</span>';
  }
  let html = '<span class="stars">';
  for (let i = 1; i <= 5; i++) {
    if (r >= i - 0.25) html += "\u2605";
    else if (r >= i - 0.75) html += "\u00BD";
    else html += '<span class="empty">\u2605</span>';
  }
  html += "</span> <small>(" + esc(r.toFixed(1)) + ")</small>";
  return html;
}

function imageHtml(url, label, large) {
  if (url) {
    return '<img src="' + esc(url) + '" alt="' + esc(label) + '" loading="lazy">';
  }
  const letters = esc((label || "JK").toUpperCase()).slice(0, 2);
  return '<div class="ph">' + letters + "</div>";
}

function stockBadge(stock) {
  const s = parseInt(stock, 10);
  if (isNaN(s)) return "";
  if (s <= 0) return '<span class="badge danger-bg">Out of stock</span>';
  if (s <= 5) return '<span class="badge accent-bg">Low stock: ' + s + "</span>";
  return '<span class="badge">In stock: ' + s + "</span>";
}

function productCard(p) {
  const url = "product.html?id=" + encodeURIComponent(p.id);
  return (
    '<article class="card product-card">' +
    '<a class="thumb" href="' + url + '">' +
    imageHtml(p.image_url, p.name) +
    "</a>" +
    '<div class="pc-body">' +
    '<span class="badge">' + esc(p.category) + "</span>" +
    '<h3 class="pc-title"><a href="' + url + '">' + esc(p.name) + "</a></h3>" +
    '<div class="pc-meta">' + starsHtml(p.rating, p.rating_count) + "</div>" +
    '<div class="pc-price">' + money(p.price) + "</div>" +
    '<div class="pc-meta">' + stockBadge(p.stock) + "</div>" +
    '<div class="pc-actions">' +
    '<a class="btn small" href="' + url + '">Details</a>' +
    '<button class="btn small outline add-cart" data-id="' + esc(p.id) +
    '" data-name="' + esc(p.name) + '">Add to Cart</button>' +
    "</div>" +
    "</div>" +
    "</article>"
  );
}

function renderGrid(container, products) {
  if (!products || products.length === 0) {
    container.innerHTML =
      '<div class="empty"><h3>No products found</h3><p>Try adjusting your search or filters.</p></div>';
    return;
  }
  container.innerHTML = products.map(productCard).join("");
}

function skeletonCards(n) {
  let out = "";
  for (let i = 0; i < n; i++) {
    out +=
      '<div class="card product-card"><div class="thumb skeleton"></div>' +
      '<div class="pc-body"><div style="height:14px;width:60%;" class="skeleton"></div>' +
      '<div style="height:18px;width:40%;" class="skeleton"></div></div></div>';
  }
  return out;
}

/* Toast */
let toastTimer = null;
function toast(msg, isError) {
  let el = document.getElementById("toast");
  if (!el) {
    el = document.createElement("div");
    el.id = "toast";
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.style.background = isError ? "#b91c1c" : "#0f172a";
  el.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(function () {
    el.classList.remove("show");
  }, 3200);
}

async function refreshCartCount() {
  const el = document.getElementById("cart-count");
  if (!el) return;
  if (!Auth.isLoggedIn()) {
    el.textContent = "";
    return;
  }
  try {
    const cart = await API.get("/api/cart", true);
    const n = cart.item_count || 0;
    el.textContent = n > 0 ? String(n) : "";
  } catch (e) {
    el.textContent = "";
  }
}
window.refreshCartCount = refreshCartCount;

/* Cart / wishlist actions (called from any page) */
async function addToCart(pid, qty, btn) {
  if (!Auth.isLoggedIn()) {
    window.location.href =
      "login.html?next=" + encodeURIComponent(location.pathname + location.search);
    return;
  }
  if (btn) {
    btn.dataset.label = btn.dataset.label || btn.textContent || "Add to Cart";
    btn.disabled = true;
    btn.textContent = "Adding...";
  }
  try {
    const name = btn ? btn.dataset.name : "";
    await API.post("/api/cart", { product_id: pid, quantity: qty || 1 }, true);
    toast("Added to cart" + (name ? " \u2022 " + name : ""));
    refreshCartCount();
  } catch (err) {
    toast(err.message || "Could not add to cart", true);
  } finally {
    if (btn) {
      btn.disabled = false;
      btn.textContent = btn.dataset.label;
    }
  }
}
window.addToCart = addToCart;

async function addToWishlist(pid, btn) {
  if (!Auth.isLoggedIn()) {
    window.location.href =
      "login.html?next=" + encodeURIComponent(location.pathname + location.search);
    return;
  }
  if (btn) btn.disabled = true;
  try {
    await API.post("/api/wishlist", { product_id: pid }, true);
    toast("Added to wishlist");
    if (btn) {
      btn.textContent = "\u2665 In Wishlist";
      btn.classList.add("wished");
    }
    const wc = document.getElementById("wish-count");
    if (wc) {
      try {
        const w = await API.get("/api/wishlist", true);
        wc.textContent = w.item_count > 0 ? String(w.item_count) : "";
      } catch (e) {}
    }
  } catch (err) {
    toast(err.message || "Could not add to wishlist", true);
  } finally {
    if (btn) btn.disabled = false;
  }
}
window.addToWishlist = addToWishlist;

/* Confirmation dialog */
function confirmDialog({ title, message, confirmText, danger }) {
  return new Promise((resolve) => {
    document.getElementById("confirm-title").textContent = title || "Confirm";
    document.getElementById("confirm-message").textContent = message || "Are you sure?";
    const btn = document.getElementById("confirm-ok");
    btn.textContent = confirmText || "Confirm";
    btn.className = "btn " + (danger ? "danger" : "");
    document.getElementById("confirm-modal").classList.add("show");
    btn.onclick = () => {
      document.getElementById("confirm-modal").classList.remove("show");
      resolve(true);
    };
    document.getElementById("confirm-cancel").onclick = () => {
      document.getElementById("confirm-modal").classList.remove("show");
      resolve(false);
    };
  });
}

/* Navbar */
function renderNavbar(active) {
  const user = Auth.user();
  const links = [
    { label: "Home", href: "index.html", name: "home" },
    { label: "Products", href: "products.html", name: "products" },
    { label: "Assistant", href: "assistant.html", name: "assistant" },
  ];
  if (user) {
    links.push({ label: "Wishlist", href: "wishlist.html", name: "wishlist" });
    links.push({ label: "Orders", href: "orders.html", name: "orders" });
  }
  if (user && (user.role === "SELLER" || user.role === "ADMIN")) {
    links.push({ label: "Seller Dashboard", href: "seller.html", name: "seller" });
  }
  if (user && user.role === "ADMIN") {
    links.push({ label: "Admin Dashboard", href: "admin.html", name: "admin" });
  }

  let linkHtml = links
    .map((l) =>
      '<a href="' + l.href + '" class="' + (l.name === active ? "active" : "") + '">' + l.label + "</a>"
    )
    .join("");

  let rightHtml;
  if (user) {
    rightHtml =
      '<span class="badge">' + esc(user.role) + "</span>" +
      '<a class="btn outline small" href="profile.html" onclick="return false;">' +
      esc(user.name || user.email) + "</a>" +
      '<button class="btn ghost small" id="nav-logout">Logout</button>';
  } else {
    rightHtml =
      '<a class="btn outline small" href="login.html">Login</a>' +
      '<a class="btn small" href="register.html">Register</a>';
  }

  document.getElementById("navbar").innerHTML =
    '<div class="container">' +
    '<a class="brand" href="index.html">JK<span> Mart</span></a>' +
    '<div class="nav-links">' + linkHtml + "</div>" +
    '<div class="nav-search"><form action="products.html" method="get">' +
    '<input type="text" name="q" placeholder="Search products...">' +
    '<button type="submit">Search</button></form></div>' +
    '<div class="nav-right">' +
    '<a class="btn ghost small nav-cart" href="cart.html" title="View cart">Cart' +
    '<span class="cart-badge" id="cart-count"></span></a>' +
    rightHtml +
    "</div></div>";

  refreshCartCount();
  const logout = document.getElementById("nav-logout");
  if (logout) {
    logout.addEventListener("click", async () => {
      await Auth.logout();
      window.location.href = "index.html";
    });
  }
}

function initCommon(active) {
  renderNavbar(active);
  document.body.addEventListener("click", (e) => {
    const btn = e.target.closest(".add-cart");
    if (!btn) return;
    e.preventDefault();
    addToCart(btn.dataset.id, parseInt(btn.dataset.qty || "1", 10) || 1, btn);
  });
  document.body.addEventListener("click", (e) => {
    const btn = e.target.closest(".wish-btn");
    if (!btn) return;
    e.preventDefault();
    addToWishlist(btn.dataset.id, btn);
  });
  document.body.addEventListener("click", (e) => {
    if (e.target.id === "confirm-cancel" || e.target.classList.contains("modal-backdrop")) {
      document.getElementById("confirm-modal").classList.remove("show");
    }
  });
}