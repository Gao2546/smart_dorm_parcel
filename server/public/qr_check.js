document.addEventListener("DOMContentLoaded", () => {
  const resultDiv = document.getElementById("result");
  const trackingInput = document.getElementById("tracking-input");
  const checkButton = document.getElementById("check-tracking");
  const logoutButton = document.getElementById("logout-button");
  const toggleDarkButton = document.getElementById("toggle-dark");
  const imgQrIcon = document.getElementById("qr-icon");
  const imgMoonIcon = document.getElementById("moon-icon");
  const imgLogoutIcon = document.getElementById("logout-icon");

  // === APPLY DARK MODE ===
    function applyDarkMode(enabled) {
    if (enabled) {
      document.body.classList.add("dark");
      imgQrIcon.src = "/qr-coded.png";
      imgMoonIcon.src = "/moond.png";
      imgLogoutIcon.src = "/logoutd.png";
    } else {
      document.body.classList.remove("dark");
      imgQrIcon.src = "/qr-code.png";
      imgMoonIcon.src = "/moon.png";
      imgLogoutIcon.src = "/logout.png";
    }
  }

  // === FETCH USER ===
//   async function fetchUser() {
//   try {
//     const res = await fetch("/me");
//     if (res.redirected) {
//       window.location.href = res.url;
//       return;
//     }
//     if (res.ok) {
//       const data = await res.json();
//       userId = data.user.id;

//       // ✅ แสดงเลขหอพัก
//       const dormElement = document.createElement("p");
//       dormElement.textContent = `🏠 Dorm Number: ${data.user.dorm}`;
//       document.querySelector(".header").appendChild(dormElement);

//       await loadDarkMode();
//     }
//   } catch (err) {
//     console.error("Could not fetch user:", err);
//   }
// }

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

  // === Manual Tracking Check ===
  checkButton.addEventListener("click", () => {
    const trackingNumber = trackingInput.value.trim();
    if (!trackingNumber) {
      resultDiv.textContent = "⚠ Please enter tracking number.";
      return;
    }
    checkTracking(trackingNumber);
  });

  async function checkTracking(trackingNumber) {
    try {
      const res = await fetch("/tracking/status", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackingNumber })
      });

      if (!res.ok) {
        const err = await res.json();
        resultDiv.textContent = `❌ ${err.error}`;
        return;
      }

      const data = await res.json();
      resultDiv.style.display = "block";
      resultDiv.textContent =
        `✅ Tracking ${trackingNumber} → Status: ${data.status}, Dorm: ${data.dormInfo?.dorm_number || "N/A"}`;
    } catch (err) {
      resultDiv.textContent = `❌ Error: ${err.message}`;
    }
  }

  // === QR SCAN ===
  const startScanBtn = document.getElementById("start-scan");
  const stopScanBtn = document.getElementById("stop-scan");
  const video = document.getElementById("preview");
  const canvas = document.getElementById("qr-canvas");
  const ctx = canvas.getContext("2d");

  let stream = null;
  let scanning = false;

  startScanBtn.addEventListener("click", async () => {
    try {
      try {
          stream = await navigator.mediaDevices.getUserMedia({
            video: { facingMode: "environment" }   // กล้องหลัง
          });
        } catch (e) {
          console.warn("Fallback to any camera:", e);
          stream = await navigator.mediaDevices.getUserMedia({ video: true });
        }
      video.srcObject = stream;
      scanning = true;
      requestAnimationFrame(scanLoop);
    } catch (err) {
      console.error("Camera error:", err);
      resultDiv.textContent = `❌ Camera error: ${err.message}`;
    }
  });

  stopScanBtn.addEventListener("click", () => {
    if (stream) {
      stream.getTracks().forEach(track => track.stop());
      stream = null;
    }
    scanning = false;
    console.log("QR scanning stopped.");
  });

  function scanLoop() {
    if (!scanning) return;

    if (video.readyState === video.HAVE_ENOUGH_DATA) {
      canvas.width = video.videoWidth;
      canvas.height = video.videoHeight;
      ctx.drawImage(video, 0, 0, canvas.width, canvas.height);

      const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
      const code = jsQR(imageData.data, imageData.width, imageData.height);

      if (code) {
        resultDiv.textContent = `Scanned: ${code.data}`;
        checkTracking(code.data);
        scanning = false;
        stopScanBtn.click(); // ปิดกล้องอัตโนมัติหลังเจอ QR
        return;
      }
    }
    requestAnimationFrame(scanLoop);
  }
  loadDarkMode();
});
