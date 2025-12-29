import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router } from '@angular/router';

import { MatCardModule } from '@angular/material/card';
import { MatInputModule } from '@angular/material/input';
import { MatButtonModule } from '@angular/material/button';

import { AuthService } from '../auth/auth';

@Component({
  selector: 'app-login',
  standalone: true,
  templateUrl: './login.html',
  styleUrls: ['./login.scss'],
  imports: [
    CommonModule,        
    FormsModule,        
    MatCardModule,      
    MatInputModule,     
    MatButtonModule     
  ]
})
export class LoginComponent {

  username = '';
  password = '';
  error = '';

  constructor(
    private auth: AuthService,
    private router: Router
  ) {}

  login() {
    this.auth.login(this.username, this.password).subscribe({
      next: res => {
        if (res.ok) {
          this.router.navigate(['/home']);
        } else {
          this.error = 'Credenciales inválidas';
        }
      },
      error: () => {
        this.error = 'Error al conectar con el servidor';
      }
    });
  }
}
