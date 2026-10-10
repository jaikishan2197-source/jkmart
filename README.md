# JK Mart — Full-Stack E-Commerce

A complete e-commerce web application: **C++20 backend (Drogon + libpq/PostgreSQL)** and a
lightweight **plain HTML/CSS/JS frontend** served by the same backend. Includes a built-in AI
shopping assistant, role-based storefront for Buyers / Sellers / Admins, wishlist, cart,
checkout, orders, reviews, and hardened security.

---

## Problem Statement

Building a production-quality mini web shop that demonstrates a non-Python/non-JS backend is
rare. Most e-commerce demos use Node / Python / PHP. This project answers: *can a full
e-commerce experience (catalog, auth, roles, carts, orders, reviews, AI assistant) be built
in modern C++ with a fast asynchronous HTTP framework, a raw PostgreSQL driver, and no
build-time JS framework?* It also exercises security hardening (input validation, role-based
authorization, clean error handling with no information leaks) and deployment.

## Objectives

- Implement a working storefront + three role dashboards (buyer, seller, admin).
- Keep the backend in C++20 (Drogon) with raw libpq for PostgreSQL.
- No hardcoded secrets, no SQL/stack-trace leaks, safe JSON error responses.
- Ship a built-in AI assistant with provider support and a guaranteed local fallback.
- Deliver a tested, publicly reachable deployment and full documentation.

## Features

- Authentication & registration (PBKDF2-SHA256 password hashes, brute-force-locked login endpoint returns generic errors only).
- Role-based access: `BUYER`, `SELLER`, `ADMIN` with per-endpoint authorization.
- Catalog with categories, search, sort, live stock and ratings.
- Wishlist (idempotent) and cart (quantity validation, stock checks, in-stock PNG not needed).
- Checkout with transactional order creation and stock decrement.
- Order lifecycle: PLACED → CONFIRMED → PROCESSING → SHIPPED → DELIVERED (seller/admin drives; buyer reads).
- Reviews: only purchasers who received the item can review; one review per user per product; rating aggregation on product.
- Admin moderation: user activate/suspend, product activate/deactivate, view all users/orders/reviews.
- AI shopping assistant (`/api/assistant/ask`) with rate limiting and provider fallback.
- INR (₹) prices, Indian number formatting.

## Architecture

```
Browser (plain HTML/CSS/JS pages)
        │  HTTP/HTTPS (same-origin JSON API)
        ▼
Drogon (C++20 async web server, port 8080)
  ├─ routes: /api/auth, /api/products, /api/cart, /api/wishlist,
  │          /api/orders, /api/checkout, /api/seller/*, /api/admin/*,
  │          /api/assistant/ask
  ├─ static files: app/public (index, products, product, cart, checkout,
  │                wishlist, orders, order, seller, admin, assistant, register, login)
  └─ libpq → PostgreSQL (jkmart database)
```

- Frontend and API are served by one Drogon process → no CORS needed for normal use.
- DB access goes through a thin async wrapper (`Db`) using parameterised statements only.

## Tech Stack

| Layer      | Technology                                              |
|------------|---------------------------------------------------------|
| Backend    | C++20, Drogon 1.9.x (async HTTP), CMake + Ninja, MinGW GCC 16 (ucrt) |
| Database   | PostgreSQL 17 + libpq (raw, no ORM)                     |
| Frontend   | Plain HTML5, CSS3, Vanilla JavaScript (ES6), no framework, no build step |
| AI         | OpenAI-compatible chat API (optional) + local fallback  |
| Deployment | Windows host + Cloudflare Quick Tunnel (public HTTPS)   |

## Database

Schema (created automatically at startup if missing):

- `users(id, name, email UNIQUE, password_hash, role[BUYER|SELLER|ADMIN], is_active)`
- `products(id, seller_id→users, name, description, price NUMERIC, stock INT, category, image_url, is_active)`
- `cart_items(buyer_id, product_id, quantity)` — per-user cart
- `wishlist_items(buyer_id, product_id)` — per-user wishlist
- `orders(id, buyer_id, status, total, payment_method, created_at)`
- `order_items(id, order_id, product_id, product_name, price, quantity)` — price snapshot at purchase
- `reviews(id, product_id, user_id, rating 1..5, title, comment, created_at)` — UNIQUE(user_id, product_id)

**Seed demo users** (password: `buyer123` / `seller123`):

| Email            | Role   | Password   |
|------------------|--------|------------|
| `tara@jk.test`   | ADMIN  | `buyer123` |
| `sami@jk.test`   | SELLER | `seller123`|
| `zee@jk.test`    | BUYER  | `buyer123` |

**AI provider config (environment variables):**

| Variable          | Meaning                                   |
|-------------------|-------------------------------------------|
| `JK_AI_API_URL`   | OpenAI-compatible base URL (unset = local mode) |
| `JK_AI_API_KEY`   | API key (never written to code)           |
| `JK_AI_MODEL`     | Model name (default `gpt-4o-mini`)        |

**PostgreSQL connection (environment variables):**
`PGHOST`, `PGPORT`, `PGDATABASE`, `PGUSER`, `PGPASSWORD`.

## Modules

### Buyer
Login/register → browse catalog (search/sort/categories) → product detail with rating →
wishlist → add-to-cart → checkout → order history → track order status → review a received
item. Each buyer only ever sees their own cart, wishlist, and orders.

### Seller
Seller dashboard: list own products with stats, create/edit/deactivate products, see orders
that include own items, advance order status. Sellers cannot touch another seller's products
or orders.

### Admin
Admin dashboard: all users (suspend/activate, cannot self-suspend), all products
(activate/deactivate), all orders, all reviews, single endpoint to reset a suspended user.

### Cart / Wishlist / Orders
- Cart: quantity must be 1..1,000,000 and ≤ available stock; qty `0` removes the line; totals computed server-side.
- Wishlist: idempotent adds (`ON CONFLICT DO NOTHING`).
- Orders: transactional checkout decreases stock and clears the cart; totals are snapshots;
  only the owning buyer / involved seller / admin can see or mutate.

### Reviews
- Only a buyer with a **delivered** order containing the product can review.
- One review per (user, product) — duplicates rejected (`409`).
- Rating aggregates back onto the product listing (average + count).
- `rating` is strictly validated (integer 1..5, parse-safe).

### AI
`POST /api/assistant/ask { "message": "..." }`
- Local mode (no provider configured): answers from the real catalog — recommendations,
  budget ("products under ₹500"), category, stock, delivery FAQ, brand/general chat.
- Provider mode: forwards the user message + a sanitised product context (name, category,
  price, stock, rating) to the configured AI endpoint; never sends tokens, identity, or secrets.
- If the provider is unreachable/slow/non-JSON: a safe local fallback answers instead.
- Rate limit: **20 requests / 60 s per client IP** → HTTP `429` with a clean JSON body.
- Input trimmed, capped at 500 characters.

## Security

- **Passwords**: PBKDF2-SHA256 with per-user salt (never stored/returned in plain text).
- **SQL injection**: every query is parameterised (`$1, $2, ...`); the only dynamic SQL is
  safe string-building of `LIKE`/`LIMIT` placeholders.
- **ID handling**: route IDs validated to be positive integers (≤12 digits); malformed IDs
  return clean `404`, never a PG error dump.
- **Error handling**: server-side failures return a generic `internal server error`; raw DB
  text / stack traces / SQL are never exposed to clients.
- **Authorization**: each mutating endpoint re-checks the caller role and object ownership.
- **Login security**: identical generic error for bad email vs bad password (no account
  enumeration); suspended accounts blocked; rate-limited AI.
- **No hardcoded secrets**: DB password and AI key are environment variables only.
- **XSS hygiene**: frontend escapes all user/product content before rendering.

## Testing

Automated suite (`jk-phase6-tests.ps1`, PowerShell 5.1, `Api()` JSON helper):

- `-Mode full`: **112 checks** — auth/registration edge cases, malformed IDs & SQL-leak
  detection, authorization/IDOR, quantity/price/image validation, buyer/seller/admin flows,
  reviews, cart/order regression, AI local answers, AI rate limiting (429).
- `-Mode fallback`: **9 checks** — AI provider-dead fallback (200, `source=fallback`,
  no secret leak, clean 400s, server stays alive).

Result: **full 112/112 PASS**, **fallback 9/9 PASS** — run both against the local server and
against the public HTTPS URL during Phase 7.

Manual demo checklist exercised live on the public URL: Buyer (login → products → detail →
wishlist → cart → checkout → order → review), Seller (login → add/edit product → appears in
marketplace → sees/advances orders), Admin (login → users → products → orders), AI question,
and **data persistence** (products/orders/reviews survive refresh and server restarts —
PostgreSQL).

## Deployment

Current deploy topology (this machine, public via Cloudflare Quick Tunnel):

1. **PostgreSQL 17** local service, database `jkmart`.
2. **Backend** `app\build\jkmart_backend.exe` launched by `app\start-prod.bat`
   (reads `PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD`, defaults to local dev DB).
3. **Frontend** served by the same Drogon process from `app\public` (cache-busted assets).
4. **Public HTTPS URL** via an account-less Cloudflare Quick Tunnel:
   `cloudflared tunnel --url http://127.0.0.1:8080`
   (gives a `*.trycloudflare.com` URL; no uptime guarantee, intended for live demo/testing).

No changes to the app are required when moving hosts: DB connection and AI key are
environment-driven, and the frontend calls the API with relative paths (`/api/…`), so it
works under any public URL/host. CORS is unnecessary in the default same-origin setup.

**IMPORTANT for the live URL:** the tunnel URL is ephemeral and dies with the `cloudflared`
process. It reflects only what is reachable while this PC stays on and the processes run.
For a permanent production domain, use a hosting provider with a named Cloudflare tunnel or
place `cloudflared` behind a VPS.

## Limitations

- Sessions are **in-memory** on the backend: a server restart logs users out (re-login
  required); not horizontally scalable across multiple instances.
- Demo checkout only (`payment_method = "DEMO"`); there is no real payment gateway.
- Cloudflare Quick Tunnel is temporary (no uptime SLA); this is a demo deployment, not a
  SLA-backed production site.
- AI provider falls back gracefully, but genuinely better answers require a real API key.
- Frontend is intentionally framework-free (fast, dependency-light) — heavier SPA tooling
  is out of scope by design.

## Future Scope

- Persistent sessions (DB or Redis-backed) for restart-safe login and scaling.
- Real payments (Razorpay / Stripe / UPI) and invoices.
- Order cancellation/returns and refund handling.
- Product images/CDN, richer search (FTS/tags), sorting by rating/price.
- Named Cloudflare tunnel or a VPS + supervised systemd service for a permanent URL.
- Seller sign-up flows, admin audit logs, and email notifications on order status.
- AI assistant memory of recent messages and per-user personalisation.

---

### Run locally

```powershell
# 1. ensure PostgreSQL is up and database jkmart exists with schema allowed
# 2. start the backend (defaults to local PG, port 8080)
C:\tools\jkmart\app\start-prod.bat
# 3. open http://127.0.0.1:8080   (or run the Cloudflare tunnel for a public URL)
```

Demo accounts are listed in the Database section above.