document.addEventListener("DOMContentLoaded", () => {
  const form = document.querySelector("form");
  const messageArea = document.getElementById("message-area");

  // Helper function to display messages
  function showMessage(message, isError = false) {
    messageArea.textContent = message;
    if (isError) {
      messageArea.style.color = "red";
    } else {
      messageArea.style.color = "green";
    }
    // Clear the message after a few seconds if it's not a success message that redirects
    if (!isError) {
      setTimeout(() => {
        messageArea.textContent = "";
        messageArea.style.color = "";
      }, 5000);
    }
  }

  form.addEventListener("submit", async (e) => {
    e.preventDefault();

    const username = form.querySelector('input[name="username"]').value.trim();
    const password = form.querySelector('input[name="password"]').value.trim();

    try {
      const response = await fetch("/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ username, password }),
      });

      const data = await response.json();

      if (response.ok) {
        if (data.user.role === "admin") {
          window.location.href = "/admin"; // ✅ redirect ไปหน้า admin
        } else {
          window.location.href = "/"; // ✅ redirect ปกติ
        }
      } else {
        showMessage("Error: " + data.error, true);
      }
    } catch (err) {
      console.error("Login request failed:", err);
      showMessage("An error occurred. Check console for details.", true);
    }
  });

  // === QR BUTTON ===
  const qrButton = document.getElementById("qr-button");
  if (qrButton) {
    qrButton.addEventListener("click", () => {
      window.location.href = "/qr_check";
    });
  }

});