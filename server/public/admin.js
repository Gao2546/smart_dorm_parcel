document.addEventListener("DOMContentLoaded", async () => {
  const logoutButton = document.getElementById("logout-button");
  const toggleDarkButton = document.getElementById("toggle-dark");
  const imgMoonIcon = document.getElementById("moon-icon");
  const imgLogoutIcon = document.getElementById("logout-icon");
  const imgQrIcon = document.getElementById("qr-icon");

  // === QR BUTTON ===
  const qrButton = document.getElementById("qr-button");
  if (qrButton) {
    qrButton.addEventListener("click", () => {
      window.location.href = "/qr_check";
    });
  }

  // === LOGOUT ===
  logoutButton.addEventListener("click", async () => {
    try {
      const res = await fetch("/logout", { method: "POST" });
      if (res.ok) {
        window.location.href = "/login";
      }
    } catch (err) {
      console.error("Logout failed:", err);
    }
  });

  // === TOGGLE DARK MODE ===
  function applyDarkMode(enabled) {
    if (enabled) {
      document.body.classList.add("dark");
      imgMoonIcon.src = "/moond.png";
      imgLogoutIcon.src = "/logoutd.png";
      imgQrIcon.src = "/qr-coded.png";
    } else {
      document.body.classList.remove("dark");
      imgMoonIcon.src = "/moon.png";
      imgLogoutIcon.src = "/logout.png";
      imgQrIcon.src = "/qr-code.png";
    }
  }
  
  async function loadDarkMode() {
    try {
      const res = await fetch("/darkmode");
      if (res.ok) {
        const data = await res.json();
        applyDarkMode(data.darkMode);
      }
    } catch (err) {
      console.error("Could not load dark mode:", err);
    }
  }
  if (toggleDarkButton) {
    toggleDarkButton.addEventListener("click", async () => {
      const isDark = document.body.classList.contains("dark");
      const newState = !isDark;
      applyDarkMode(newState);

      try {
        await fetch("/darkmode", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ darkMode: newState }),
        });
      } catch (err) {
        console.error("Could not save dark mode:", err);
      }
    });
  }

  loadDarkMode();

  // === FETCH USERS & TRACKING DATA ===
  async function fetchUsers() {
    const res = await fetch("/api/admin/users");
    return res.json();
  }

  async function fetchTracking() {
    const res = await fetch("/api/admin/tracking");
    return res.json();
  }

  function renderUsers(users) {
    const list = users
      .map(u => `<li>${u.username} (Dorm: ${u.dorm_number}, Role: ${u.role})</li>`)
      .join("");
    document.getElementById("users-list").innerHTML = `<ul>${list}</ul>`;
  }

  function renderTracking(tracks) {
    const list = tracks
      .map(t => `<li>${t.tracking_number} - ${t.status} (User: ${t.username}, Dorm: ${t.dorm_number})</li>`)
      .join("");
    document.getElementById("tracking-list").innerHTML = `<ul>${list}</ul>`;
  }

  renderUsers(await fetchUsers());
  renderTracking(await fetchTracking());
});
