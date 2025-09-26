document.addEventListener("DOMContentLoaded", () => {
  const form = document.querySelector("form");
  const messageArea = document.getElementById("message-area"); // Get the message area element

  // Helper function to display messages
  function showMessage(message, isError = false) {
    messageArea.textContent = message;
    // You can add different classes for styling success vs. error messages
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

    // Get form data
    const username = form.querySelector('input[name="username"]').value.trim();
    const password = form.querySelector('input[name="password"]').value.trim();
    // const dorm = form.querySelector('input[name="dorm"]').value.trim();
    const dorm = form.querySelector('select[name="dorm"]').value.trim();

    if (!username || !password || !dorm) {
      showMessage("Please fill in all fields", true); // Use showMessage for errors
      return;
    }

    try {
      const response = await fetch("/register", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ username, password, dorm }),
      });

      const data = await response.json();

      if (response.ok) {
        showMessage("Registration successful! Redirecting...", false); // Use showMessage for success
        // Give the user a moment to see the message before redirecting
        setTimeout(() => {
          window.location.href = "/login";
        }, 100);
      } else {
        showMessage("Error: " + data.error, true); // Use showMessage for errors
      }
    } catch (err) {
      console.error("Register request failed:", err);
      showMessage("An error occurred. Check console for details.", true); // Use showMessage for errors
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