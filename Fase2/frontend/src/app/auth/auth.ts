import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable, tap } from 'rxjs';
//import { environment } from '../../../environment';

export interface LoginResponse {
  ok: boolean;
  username: string;
  is_admin: boolean;
}

@Injectable({
  providedIn: 'root'
})
export class AuthService {

  private API_URL = 'http://192.168.0.25:18080/login';

  constructor(private http: HttpClient) {}

  login(username: string, password: string): Observable<LoginResponse> {
    return this.http.post<LoginResponse>(this.API_URL, {
      username,
      password
    }).pipe(
      tap(res => {
        if (res.ok) {
          localStorage.setItem('user', JSON.stringify(res));
        }
      })
    );
  }

  logout() {
    localStorage.removeItem('user');
  }

  getUser() {
    const user = localStorage.getItem('user');
    return user ? JSON.parse(user) : null;
  }

  isAdmin(): boolean {
    return this.getUser()?.is_admin === true;
  }

  isLogged(): boolean {
    return this.getUser() !== null;
  }
}
