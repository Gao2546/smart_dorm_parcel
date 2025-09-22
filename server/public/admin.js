document.addEventListener("DOMContentLoaded", async () => {
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
