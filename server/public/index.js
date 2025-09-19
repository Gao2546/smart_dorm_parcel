document.addEventListener("DOMContentLoaded", () => {
  const form = document.querySelector(".tracking-form");
  const input = form.querySelector('input[name="number"]');
  const trackingList = document.querySelector(".muted");
  const logoutButton = document.getElementById("logout-button");
  const toggleDarkButton = document.getElementById("toggle-dark");

  let userId;

  // === APPLY DARK MODE ===
  function applyDarkMode(enabled) {
    if (enabled) {
      document.body.classList.add("dark");
      toggleDarkButton.textContent = "☀️"; // switch icon to sun
    } else {
      document.body.classList.remove("dark");
      toggleDarkButton.textContent = "🌙"; // switch icon to moon
    }
  }

  // === FETCH USER ===
  async function fetchUser() {
    try {
      const res = await fetch("/me");
      if (res.redirected) {
        window.location.href = res.url;
        return;
      }
      if (res.ok) {
        const data = await res.json();
        userId = data.user.id;
        await loadTrackingNumbers();
        await loadDarkMode(); // ✅ load dark mode when user loads
      }
    } catch (err) {
      console.error("Could not fetch user:", err);
    }
  }

  // === LOAD DARK MODE FROM SESSION ===
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

  // === TOGGLE DARK MODE ===
  if (toggleDarkButton) {
    toggleDarkButton.addEventListener("click", async () => {
      const isDark = document.body.classList.contains("dark");
      const newState = !isDark;
      applyDarkMode(newState);

      try {
        await fetch("/darkmode", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ enabled: newState }),
        });
      } catch (err) {
        console.error("Failed to save dark mode:", err);
      }
    });
  }

  // === LOAD TRACKING NUMBERS ===
  async function loadTrackingNumbers() {
    if (!userId) return;
    try {
      const res = await fetch("/tracking_number", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ userId }),
      });

      if (!res.ok) throw new Error(`HTTP error! status: ${res.status}`);

      const data = await res.json();
      displayTrackingNumbers(data.trackingNumbers);
    } catch (err) {
      console.error("Error loading tracking numbers:", err);
    }
  }

  // === DISPLAY TRACKING NUMBERS ===
  function displayTrackingNumbers(numbers) {
    if (!numbers || numbers.length === 0) {
      trackingList.textContent = "No tracking numbers added yet.";
      return;
    }

    const list = numbers
      .map(
        (n) => `
        <li>
          <span class="tracking-number">${n.tracking_number}</span> - 
          <span class="tracking-status">${n.status || "pending"}</span>
          <button class="delete-btn" data-number="${n.tracking_number}">❌</button>
        </li>`
      )
      .join("");
    trackingList.innerHTML = `<ul>${list}</ul>`;

    document.querySelectorAll(".delete-btn").forEach((btn) => {
      btn.addEventListener("click", async (e) => {
        const trackingNumber = e.target.dataset.number;
        await deleteTrackingNumber(trackingNumber);
      });
    });
  }

  // === DELETE TRACKING NUMBER ===
  async function deleteTrackingNumber(trackingNumber) {
    if (!trackingNumber) return;
    try {
      const res = await fetch("/tracking", {
        method: "DELETE",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackingNumber }),
      });

      const data = await res.json();
      if (res.ok) {
        await loadTrackingNumbers();
      } else {
        alert("Error deleting tracking number: " + data.error);
      }
    } catch (err) {
      console.error("Delete request failed:", err);
    }
  }

  // === ADD NEW TRACKING NUMBER ===
  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const trackingNumber = input.value.trim();
    if (!trackingNumber) return;

    try {
      const res = await fetch("/tracking", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackingNumber }),
      });

      const data = await res.json();
      if (res.ok) {
        input.value = "";
        await loadTrackingNumbers();
      } else {
        alert("Error adding tracking number: " + data.error);
      }
    } catch (err) {
      console.error("Tracking request failed:", err);
    }
  });

  // === LOGOUT ===
  if (logoutButton) {
    logoutButton.addEventListener("click", async () => {
      try {
        const res = await fetch("/logout", { method: "POST" });
        if (res.ok) window.location.href = "/login";
        else alert("Logout failed. Please try again.");
      } catch (err) {
        console.error("Logout request failed:", err);
      }
    });
  }

  // Initial load
  fetchUser();
});
