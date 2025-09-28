document.addEventListener("DOMContentLoaded", () => {
  const resultDiv = document.getElementById("result");
  const trackingInput = document.getElementById("tracking-input");
  const checkButton = document.getElementById("check-tracking");
  const logoutButton = document.getElementById("logout-button");
  const toggleDarkButton = document.getElementById("toggle-dark");
  const imgMoonIcon = document.getElementById("moon-icon");
  const imgLogoutIcon = document.getElementById("logout-icon");

  // === APPLY DARK MODE ===
  function applyDarkMode(enabled) {
    if (enabled) {
      document.body.classList.add("dark");
      if (imgMoonIcon) imgMoonIcon.src = "/moond.png";
      if (imgLogoutIcon) imgLogoutIcon.src = "/logoutd.png";
    } else {
      document.body.classList.remove("dark");
      if (imgMoonIcon) imgMoonIcon.src = "/moon.png";
      if (imgLogoutIcon) imgLogoutIcon.src = "/logout.png";
    }
  }

  // === LOGOUT ===
  if (logoutButton) {
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
  if (checkButton && trackingInput && resultDiv) {
    checkButton.addEventListener("click", () => {
      console.log("Check button clicked"); // Debug log
      const trackingNumber = trackingInput.value.trim();
      if (!trackingNumber) {
        resultDiv.style.display = "block";
        resultDiv.textContent = "⚠ Please enter tracking number.";
        return;
      }
      checkTracking(trackingNumber);
    });

    // Also add Enter key support for the input field
    trackingInput.addEventListener("keypress", (e) => {
      if (e.key === "Enter") {
        checkButton.click();
      }
    });
  } else {
    console.error("Required elements not found:", {
      checkButton: !!checkButton,
      trackingInput: !!trackingInput,
      resultDiv: !!resultDiv
    });
  }

  async function checkTracking(trackingNumber) {
    console.log("Checking tracking:", trackingNumber); // Debug log
    
    if (!resultDiv) {
      console.error("Result div not found");
      return;
    }

    // Show loading state
    resultDiv.style.display = "block";
    resultDiv.textContent = "🔍 Checking...";

    try {
      const res = await fetch("/tracking/status", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackingNumber })
      });

      console.log("Response status:", res.status); // Debug log

      if (!res.ok) {
        const err = await res.json();
        resultDiv.textContent = `❌ ${err.error || 'Unknown error'}`;
        return;
      }

      const data = await res.json();
      console.log("Response data:", data); // Debug log
      
      resultDiv.style.display = "block";
      resultDiv.textContent =
        `✅ Tracking ${trackingNumber} → Status: ${data.status}, Dorm: ${data.dormInfo?.dorm_number || "N/A"}`;
    } catch (err) {
      console.error("Tracking check error:", err);
      resultDiv.style.display = "block";
      resultDiv.textContent = `❌ Error: ${err.message}`;
    }
  }

  // === QR SCAN ===
  const startScanBtn = document.getElementById("start-scan");
  const stopScanBtn = document.getElementById("stop-scan");
  const video = document.getElementById("preview");
  const canvas = document.getElementById("qr-canvas");

  let stream = null;
  let scanning = false;

  if (startScanBtn && video && canvas) {
    const ctx = canvas.getContext("2d");

    startScanBtn.addEventListener("click", async () => {
      console.log("Start scan clicked"); // Debug log
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
        if (resultDiv) {
          resultDiv.style.display = "block";
          resultDiv.textContent = `❌ Camera error: ${err.message}`;
        }
      }
    });

    if (stopScanBtn) {
      stopScanBtn.addEventListener("click", () => {
        console.log("Stop scan clicked"); // Debug log
        if (stream) {
          stream.getTracks().forEach(track => track.stop());
          stream = null;
        }
        scanning = false;
        console.log("QR scanning stopped.");
      });
    }

    function scanLoop() {
      if (!scanning) return;

      if (video.readyState === video.HAVE_ENOUGH_DATA) {
        canvas.width = video.videoWidth;
        canvas.height = video.videoHeight;
        ctx.drawImage(video, 0, 0, canvas.width, canvas.height);

        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        
        // Check if jsQR is available
        if (typeof jsQR !== 'undefined') {
          const code = jsQR(imageData.data, imageData.width, imageData.height);

          if (code) {
            console.log("QR code detected:", code.data); // Debug log
            if (resultDiv) {
              resultDiv.style.display = "block";
              resultDiv.textContent = `Scanned: ${code.data}`;
            }
            checkTracking(code.data);
            scanning = false;
            if (stopScanBtn) stopScanBtn.click(); // ปิดกล้องอัตโนมัติหลังเจอ QR
            return;
          }
        } else {
          console.error("jsQR library not loaded");
        }
      }
      requestAnimationFrame(scanLoop);
    }
  } else {
    console.error("QR scan elements not found:", {
      startScanBtn: !!startScanBtn,
      video: !!video,
      canvas: !!canvas
    });
  }

  // Initialize dark mode
  loadDarkMode();
});